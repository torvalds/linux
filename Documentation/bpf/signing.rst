.. SPDX-License-Identifier: GPL-2.0

============
BPF signing
============

This document describes how BPF programs are cryptographically signed, how the
kernel verifies them at load time, and how Linux Security Modules (LSMs) -
including the BPF LSM - use the resulting verdict to enforce policy. It is
written for developers who want to produce signed BPF objects, understand what
the signature actually guarantees, or build a policy on top of it.

Motivation
==========

A signed BPF program lets the kernel establish that the bytecode being loaded
originates from a trusted producer and was not modified in transit. On its own
the kernel does not *require* signatures - an unsigned program loads exactly as
before - but it records a verdict (see `The verdict`_) that an LSM can gate on.
This is the building block for policies such as "only run BPF that was signed by
a key in the trusted keyring", as could in the future be enforced by an LSM
such as IPE.

Signing is orthogonal to the existing permission model: it does not replace the
capability checks or the verifier. A signed load still requires the usual
privileges (``CAP_BPF`` and any program-type-specific capability, subject to
``kernel.unprivileged_bpf_disabled``), and the loader's instructions are still
checked by the verifier like any other program. A valid signature establishes
*origin and integrity*, not safety - it lets a policy trust where the bytecode
came from, it does not let a load skip any check it would otherwise face.

The hard part is *what* gets signed. A naive scheme would sign a program's
instruction buffer at build time and verify that signature at
``BPF_PROG_LOAD``. That does not survive contact with real BPF objects, because
the bytes the kernel finally loads are not the bytes the developer built and
signed. Between the two, libbpf and the kernel rewrite the program:

- **map file descriptors** are patched into ``ld_imm64`` instructions
  (``BPF_PSEUDO_MAP_FD``), and a map's fd is assigned at load time, so it
  differs on every run;
- **CO-RE relocations** rewrite field offsets, sizes and existence flags against
  the *running* kernel's BTF, so the result differs from one kernel to the next;
- **kfunc and ksym references** are resolved to ids/addresses in the running
  kernel;
- **global data** (``.rodata``/``.data``/``.bss``) is created and seeded as maps
  at load.

So a signature over the original instructions cannot match the relocated
instructions the verifier ends up checking, and the relocated form cannot be
produced ahead of time because it depends on the target kernel. There is no
fixed byte string that is both signable at build time and what the kernel
actually loads - which is why a program cannot simply be signed and loaded
directly.

The trusted loader
==================

The solution is to move that setup work *into* a small BPF program - the
**loader** - and sign the loader instead of the individual programs. libbpf's
``gen_loader`` machinery (``bpftool gen skeleton -L``, the "light skeleton")
emits a ``BPF_PROG_TYPE_SYSCALL`` program whose body performs the bpf() syscalls
that create maps, apply relocations, and load the real programs. The payload it
installs - the serialized programs, map descriptions, relocation data and
initial values - lives in a separate array map, the **metadata map**
(``__loader.map``).

So the unit of trust is the loader, and the signing contract is::

    Sig(I_loader || D_meta)

where ``I_loader`` is the loader's instruction stream and ``D_meta`` is the
content of the metadata map. Verifying the loader's signature establishes that
both the loader *and* the payload it is about to install are authentic. The
loader is reproducible: ``gen_loader`` builds it from primitives so the same
object yields the same bytes on any build host.

Why the loader is signable when the program is not
--------------------------------------------------

The loader sidesteps every rewrite listed above, because the bytes that are
signed are *relocation-invariant*:

- The loader's own instructions are a fixed sequence of bpf() syscalls emitted
  by ``gen_loader``; they carry no CO-RE relocations and resolve no ksyms, so
  they are identical on every kernel. The metadata map is referenced by *index*
  into ``fd_array`` (``BPF_PSEUDO_MAP_IDX_VALUE``), not by a baked-in file
  descriptor, so even that reference does not change between build and load.
  The loader instruction bytes the kernel verifies are exactly the bytes that
  were signed.
- The metadata map is opaque, frozen data - the serialized target programs,
  their relocation records, map descriptions and initial values. Its bytes are
  identical at build time and at load time, so they are simply appended to the
  instructions and covered by the same signature (there is no separate metadata
  hash to compute or compare).

All the host-specific rewriting - creating maps, patching their fds into the
target programs, applying CO-RE, resolving ksyms, seeding global data - still
happens, but it happens *inside the loader at runtime*, on the verified
metadata, **after** the kernel has verified the ``insns || metadata`` signature.
The kernel never has to verify the relocated target programs: it verifies the
loader and its inputs once, and trust transfers to whatever that now-trusted,
deterministic loader installs. The relocation step is moved from "before the
signature can be checked" to "after a trusted program runs" - which is exactly
what makes it signable.

Because the metadata map is the loader's only untrusted input, two existing map
properties are reused to keep it trustworthy across the load:

Exclusive maps
    A map created with ``excl_prog_hash`` (see ``BPF_MAP_CREATE``) may only be
    accessed by a program whose digest matches that hash. The verifier enforces
    ``map->excl_prog_sha == prog->digest`` for every map a program uses, so the
    metadata map is bound to exactly the signed loader and cannot be shared with
    or mutated by another program.

Frozen maps
    The metadata map is frozen (``BPF_MAP_FREEZE``) before the loader is loaded.
    Freezing blocks further userspace writes, so the bytes folded into the
    signature cannot change before the loader runs. (Freezing does not make the
    map read-only to the loader program itself, which still writes created file
    descriptors back into the blob's scratch area.)

Load-time verification
=======================

Rather than have the loader check its own metadata from within BPF, the kernel
verifies it directly at ``BPF_PROG_LOAD``, with no new UAPI. The mechanism
reuses the existing ``fd_array``:

#. Userspace creates the metadata map with ``excl_prog_hash`` set to the
   loader's digest, populates it, and freezes it.
#. The loader is loaded with ``signature``/``signature_size``/``keyring_id``
   set, the metadata map referenced through ``fd_array``, and ``fd_array_cnt``
   set so the kernel knows the array's length.
#. Signature verification runs inside the verifier (``bpf_check()``), once it
   has resolved the ``fd_array`` entries into the program's ``used_maps``. The
   maps folded into the signature are therefore the very objects the program
   binds - a single resolution of ``fd_array``, not a separate read, so the
   verified bytes cannot be swapped for a different map after the check (no
   time-of-check/time-of-use window). Each folded map must be exclusive (carry
   ``excl_prog_sha``) and a plain array map (``BPF_MAP_TYPE_ARRAY``); only an
   array map exposes its value buffer through ``map_direct_value_addr()`` as a
   kernel address spanning ``value_size`` bytes. A map that is not exclusive, not
   frozen, or not a plain array is rejected, with a verifier log message naming
   the offending map. The kernel appends each map's frozen
   contents to the instruction buffer and verifies the PKCS#7 signature over the
   concatenation ``insns || metadata_0 || metadata_1 || ...`` in ``used_maps``
   order, before it rewrites the (signed) instructions.

A signed program therefore takes one of exactly two shapes, both fully
supported:

- **No bound maps** (``fd_array_cnt == 0``): there is nothing to append, so the
  kernel verifies the signature over the instructions alone. A valid signature
  yields ``BPF_SIG_VERIFIED`` and the program loads. This is the ordinary case
  for a directly-loaded signed program with no separate payload; it is *not*
  rejected for "missing" metadata, because it has none to cover.
- **Exclusive bound maps** (``fd_array_cnt > 0``): every entry is exclusive and
  folded, so the signature covers ``insns || metadata``.

There is no third shape: a non-exclusive map in a signed program's ``fd_array``
is rejected rather than silently left out of the signature, so a signed loader
never binds a map its signature does not cover.

The digest binding (``excl_prog_sha == prog->digest``) is enforced by the
verifier as usual; because that check runs while ``fd_array`` is resolved -
before the verifier would otherwise compute the tag - ``prog->digest`` is
computed up front in the verifier, over the unmodified (signature-covered)
instructions, for any signed load.

Coverage is then enforced as the verifier resolves instructions, at the point
each object is bound rather than by a count taken afterwards. Once the signature
has been verified, binding any further map is refused: a map reached by a
directly-referenced fd, or a map swapped into an ``fd_array`` slot the loader
reads, is not among those already folded, so it is rejected the moment the
verifier tries to bind it. A BTF is refused outright for a signed program - a
ksym or a BTF fd in ``fd_array``, whether resolved up front or lazily for a
module kfunc, is rejected when it would be bound. Together with the fold rule
above this keeps the verdict binary: a signed program cannot use a map its
signature does not cover, and a different but equally digest-bound map cannot be
substituted at an ``fd_array`` slot. Non-exclusive maps are never folded, so a
signed program cannot use one at all.

The verdict
===========

A program is either unsigned or fully verified - there is no intermediate
state. The outcome is recorded in ``prog->aux->sig.verdict``:

.. code-block:: c

    enum bpf_sig_verdict {
            BPF_SIG_UNSIGNED = 0,
            BPF_SIG_VERIFIED,
    };

``BPF_SIG_VERIFIED`` means the signature is valid and covers the instructions
*and* the frozen contents of every exclusive map the program uses:

- For an ordinary, directly-loaded signed program the instructions are the whole
  artifact and it uses no exclusive maps, so a valid instruction signature is
  the complete verification.
- For a signed loader the metadata map is exclusive, so its contents are folded
  in and the signature covers ``insns || metadata``.

There is deliberately no "instructions verified but metadata not" verdict: a
signed loader that fails to cover its metadata is *rejected* (see above), not
recorded with a weaker verdict. ``BPF_SIG_VERIFIED`` therefore always means the
program and everything the signature is responsible for are authentic, which is
what a policy can rely on.

Alongside the verdict the kernel records which keyring validated the signature;
see `Keyrings`_.

Enforcement via LSMs
====================

Signing only *records* a verdict; an LSM turns it into policy. The verdict and
keyring fields live in ``struct bpf_prog_aux``, so a BPF LSM program can read
them directly (see Documentation/bpf/prog_lsm.rst for writing and attaching BPF
LSM programs); the same fields are equally available to in-tree LSMs. Two hooks
are useful at different points of the load: the dedicated
``security_bpf_prog_load()`` gates admission before the main verification work,
and the existing ``security_bpf_prog()`` observes a program that has fully
loaded.

Admission: ``security_bpf_prog_load()``
---------------------------------------

This hook gates admission **for every load**, from a single call site inside the
verifier (``bpf_check()``), before the main verification work. It runs after the
optional signature verification, so the verdict and keyring fields are final - the
hook can see whether, and how strongly, the program was signed, which keyring
validated it, the load ``attr``, the BPF token and whether the load came from the
kernel. For a signed load the verdict is ``BPF_SIG_VERIFIED`` here (the signature
has just been checked); for an unsigned load it is ``BPF_SIG_UNSIGNED``.

This is the place for *coarse admission* that must also see unsigned and
not-yet-verified loads: require a signature at all, restrict the acceptable
keyring, restrict which token/credentials may load BPF, apply per-program-type
rules, or audit every load attempt that makes it past signature verification -
attempts failing the signature or the metadata binding abort before this hook
fires. It is the primary deny point.

One subtlety: this hook runs *before* the verifier finishes its work, so
``BPF_SIG_VERIFIED`` *here* means only "validly signed" - not "loaded". Allowing
a load at this point lets it *proceed*; it does not guarantee the program will
load. A validly signed program can still be rejected afterwards on two
independent grounds: the verifier may reject it like any other program (unsafe
memory access, bad control flow, resource limits, ...), and the kernel separately
refuses - as the verifier resolves instructions and binds each object - any map
the signature does not cover or any BTF at all, regardless of what this hook
returned. Only after the program has fully loaded, at the next hook
(``security_bpf_prog()``), does ``BPF_SIG_VERIFIED`` carry its full meaning:
validly signed *and* fully verified.

A more realistic admission policy than "is it signed at all": base trust in
the bpf keyring or an explicit allowlist of a caller-supplied staging key/
keyring, and emit a record of every decision including denied attempts.
(Illustrative - error checking elided.)

.. code-block:: c

    /*
     * Serials of caller-supplied keyrings we are willing to stage. Empty
     * on a system that has committed to the bpf keyring, where the kernel
     * refuses them anyway.
     */
    struct {
            __uint(type, BPF_MAP_TYPE_HASH);
            __type(key, __s32);             /* keyring_serial */
            __type(value, __u8);
            __uint(max_entries, 64);
    } staging_keys SEC(".maps");

    /* Audit stream consumed by a userspace logger. */
    struct {
            __uint(type, BPF_MAP_TYPE_RINGBUF);
            __uint(max_entries, 1 << 16);
    } audit SEC(".maps");

    struct decision { __u32 prog_type, verdict, ktype; __s32 serial, ret; };

    SEC("lsm/bpf_prog_load")
    int BPF_PROG(admit, struct bpf_prog *prog, union bpf_attr *attr,
                 struct bpf_token *token, bool kernel)
    {
            __u32 verdict = prog->aux->sig.verdict;
            __u32 ktype   = prog->aux->sig.keyring_type;
            __s32 serial  = prog->aux->sig.keyring_serial;
            struct decision *d;
            int ret = 0;

            if (kernel)
                    return 0;                       /* trust in-kernel loads */

            if (verdict != BPF_SIG_VERIFIED) {
                    ret = -EPERM;                   /* must be validly signed */
            } else switch (ktype) {
            case BPF_SIG_KEYRING_BPF:
                    break;
            case BPF_SIG_KEYRING_USER:
                    if (!bpf_map_lookup_elem(&staging_keys, &serial))
                            ret = -EPERM;
                    break;
            default:
                    ret = -EPERM;                   /* keyring not in policy */
            }

            d = bpf_ringbuf_reserve(&audit, sizeof(*d), 0);
            if (d) {
                    d->prog_type = attr->prog_type;
                    d->verdict = verdict;
                    d->ktype = ktype;
                    d->serial = serial;
                    d->ret = ret;
                    bpf_ringbuf_submit(d, 0);       /* record allow *and* deny */
            }
            return ret;
    }

Such a policy is what makes a caller-supplied keyring usable at all on a system
that does not boot with ``bpf.keyring_unsealed=1``: the allowlist bounds which
staged keys count, and the LSM itself has to protect them from being tampered
with.

Observing a verified load: ``security_bpf_prog()``
--------------------------------------------------

There is deliberately no separate "metadata attested" hook. The coverage check
above is enforced by the kernel unconditionally, so a signed loader that fails
to cover its metadata never loads and an LSM never has to re-establish that
fact. To *act on* a program that has successfully and fully loaded, use the
existing ``security_bpf_prog()`` hook (``lsm/bpf_prog``), which fires from
``bpf_prog_new_fd()`` - after the verifier, after the coverage check, and after
``bpf_prog_alloc_id()``. Relative to the admission hook this point is strictly
later and stronger:

- the program has an id (``prog->aux->id``), so it can be recorded or correlated
  with later events;
- ``verdict == BPF_SIG_VERIFIED`` *here* means **fully** verified - a program
  that used a map the signature does not cover was already rejected, so it cannot
  reach this point;
- it observes only programs that actually loaded; a failed load never mints an
  fd, so it never reaches this hook.

It takes only the ``prog`` and a non-zero return still aborts (the fd is not
handed out), so it can veto as well as observe. One wrinkle: it also fires on
other paths that mint a new program fd - notably ``bpf_prog_get_fd_by_id()`` -
not just on a fresh load. Because the program already has its id here, an LSM
can tell the two apart with a small hash map: the *first* time an id is seen is
the load; a later sighting of the same id is just another fd to a program that
already exists.

To bound the map and let a reused id read as a fresh load, this can be paired
with ``security_bpf_prog_free()`` (``lsm/bpf_prog_free``), which deletes the
entry on teardown - keyed by the same ``prog`` pointer, since
``bpf_prog_free_id()`` has already cleared ``prog->aux->id`` to ``0`` by the time
that hook runs. (Illustrative - privileged LSM, error checking elided.)

.. code-block:: c

    struct rec { __u32 id, ktype; __s32 serial; };

    struct {
            __uint(type, BPF_MAP_TYPE_HASH);
            __type(key, __u64);             /* struct bpf_prog * -- stable id */
            __type(value, struct rec);
            __uint(max_entries, 4096);
    } live SEC(".maps");

    SEC("lsm/bpf_prog")            /* fires after load and on every later fd */
    int BPF_PROG(observe, struct bpf_prog *prog)
    {
            __u64 key = (__u64)(unsigned long)prog;
            struct rec r;

            if (prog->aux->sig.verdict != BPF_SIG_VERIFIED)
                    return 0;
            if (bpf_map_lookup_elem(&live, &key))
                    return 0;               /* seen before: a later fd, not a load */

            /* First sighting == this program just loaded; id is valid here. */
            r.id     = prog->aux->id;
            r.ktype  = prog->aux->sig.keyring_type;
            r.serial = prog->aux->sig.keyring_serial;
            bpf_map_update_elem(&live, &key, &r, BPF_NOEXIST);
            /* ... newly-loaded verified-program action, e.g. record r.id ... */
            return 0;
    }

Putting them together: to *require* verified BPF, deny at the admission hook
unless the verdict is ``BPF_SIG_VERIFIED`` (and, if desired, restrict the
keyring). The kernel then guarantees that any program which actually loads with
that verdict covered all of its exclusive maps, rejecting any that did not - so
a deny-by-default admission policy needs no second enforcement point. Use
``security_bpf_prog()`` to record or finally gate the verified programs once
they carry an id. The ``verdict``, ``keyring_type`` and ``keyring_serial`` fields
let a policy distinguish "verified against the operator's bpf keyring" from
"verified against a keyring the loader supplied itself", which is the
distinction that matters most. A policy LSM such as IPE could consume the same
hooks to enforce system policy without writing any BPF, though none implements
this today.

Keyrings
========

``keyring_id`` selects the trusted keyring the PKCS#7 signature is verified
against. Four values are well-known; anything else is resolved as a caller-
supplied key or keyring, named either by its serial or by one of the other
``KEY_SPEC_*`` ids:

.. list-table::
   :header-rows: 1

   * - ``keyring_id``
     - Keyring
   * - ``0``
     - builtin trusted keyring
   * - ``VERIFY_USE_SECONDARY_KEYRING`` (``1``)
     - secondary trusted keyring
   * - ``VERIFY_USE_PLATFORM_KEYRING`` (``2``)
     - platform keyring
   * - ``KEY_SPEC_BPF_KEYRING`` (``-9``)
     - the bpf keyring
   * - anything else
     - a caller-supplied user/session key or keyring

The keyring is resolved first, before the signature bytes are examined, so a
signature naming a keyring that cannot be used is rejected up front, and a
failed verification aborts the load - a program that loads successfully with
a signature therefore always has consistent keyring fields recorded.

The bpf keyring
---------------

A system keyring needs a kernel rebuild or a vouched-for enrollment to rotate a
key, and grants BPF-signing trust to keys trusted for everything else in the
kernel too. A caller-supplied keyring, at the other extreme, is filled by the
very process that loads the program and so carries no trust of its own.

The bpf keyring fills that gap and is the trust anchor which a signed BPF
deployment should be built on top of: a keyring named ``.bpf``, selected with
``KEY_SPEC_BPF_KEYRING``, that an operator provisions at boot with a key
scoped to BPF program loading and nothing else in the kernel's trust hierarchy.
It is owned by the operator rather than by the loader, and rotatable across a
reboot without touching the kernel image. It is modelled after the dm-verity
keyring (see ``dm_verity.keyring_unsealed=``) and provisioned the same way: an
initrd runs the ``keyctl`` steps below before handing off to the rootfs.

Provisioning
~~~~~~~~~~~~

The keyring is created during ``late_initcall`` and is **sealed empty** by
default: it carries a reject-all restriction, so no key can ever be added and
``KEY_SPEC_BPF_KEYRING`` fails with ``-ENOKEY`` for the whole boot.

``bpf.keyring_unsealed=1`` leaves it unrestricted at init so the initrd can
provision it. The keyring is not linked into any process keyring, but the
``KEY_SPEC_BPF_KEYRING`` special key id addresses it directly, so its serial
does not have to be looked up first. Steps would be as follows::

    keyctl padd asymmetric "" -9 < signing_key.der
    keyctl restrict_keyring -9

Both steps are required: the keyring is consulted only once it is **non-empty
and restricted**. An unrestricted keyring is ignored even when it holds keys,
so a half-provisioned keyring is inert rather than a weaker trust anchor, and a
load naming it fails with ``-ENOKEY`` and a verifier log. Restricting cannot
be undone.

More than one key is enrolled by repeating the ``keyctl padd`` step; the
restriction is applied once, after the last of them::

    for key in /etc/bpf/keys/*.der; do
        keyctl padd asymmetric "" -9 < $key
    done

    keyctl restrict_keyring -9
    keyctl show -9

The restriction bounds what can be added, never what can be taken away. A key
that is already enrolled can still be unlinked, and the keyring cleared or
revoked, by anything running as root. That does not weaken the anchor, since
a keyring left empty is no longer consulted and a load naming it fails with
``-ENOKEY``, but it does take signed loading out until the next boot. Dropping
the user permissions the keyring no longer needs would close that; as a third
step in the initrd::

    keyctl setperm -9 0x08030000

What remains is ``KEY_POS_SEARCH`` for the in-kernel search during verification,
plus ``KEY_USR_VIEW`` and ``KEY_USR_READ`` so the keyring stays visible in
``/proc/keys``. ``KEY_SPEC_BPF_KEYRING`` names the keyring but conveys no rights
of its own - the keyring is not possessed by anyone, so the ``KEY_POS_*`` bits
are only ever reached by the in-kernel search, and userspace is left with what
the ``KEY_USR_*`` bits grant. Once they are dropped, addressing it by the serial
``/proc/keys`` reports gets no further than the special id does.

Provisioning has to complete before control passes to the rootfs. The keyring
takes any number of keys for as long as it is unrestricted, so it is the
restriction that bounds the enrolled set, not the first enrollment. Before
the initrd hands off control, it must therefore restrict the keyring after
the last enrollment.

Enforcement
~~~~~~~~~~~

``bpf.keyring_unsealed=1`` states that the bpf keyring is *the* trust anchor for
this boot, so it does more than unseal. From the first program load onwards a
caller-supplied user/session keyring is refused with ``-EPERM`` and a verifier
log message, whether or not provisioning ever completed. The system keyrings
stay selectable.

Enforcement is readable at ``/sys/module/bpf/parameters/keyring_unsealed``, and
read-only there: the flag is ``__ro_after_init`` behind a 0444 parameter. It is
also derived from the boot flag rather than from the keyring's runtime state,
so there is no window early in boot during which a caller-supplied keyring is
still accepted.

Caller-supplied keyrings are for staging
----------------------------------------

A ``keyring_id`` naming a user or session key or keyring is a *staging*
mechanism, not a trust anchor: it is filled by the same userspace that loads the
program, so verifying against it establishes only that the loader signed what it
loaded. Its purpose is to let software installed onto a running system - whose
signing key is not enrolled anywhere yet - run signed until that key reaches the
bpf keyring on the next boot.

A system that has committed to the bpf keyring refuses this path outright (see
`Enforcement`_). A system that has not can still allow it, but a policy must
never treat ``BPF_SIG_KEYRING_USER`` as equivalent to the bpf or system
keyrings; it should allowlist the specific serials it is willing to stage and
pair that with a BPF LSM policy protecting those keys from tampering, as in
`Enforcement via LSMs`_.

Recorded fields
---------------

Two fields are recorded in ``prog->aux->sig`` for an LSM to inspect:

``keyring_type`` (``enum bpf_sig_keyring``)
    Classified purely from ``keyring_id`` whenever the program is signed:
    ``BPF_SIG_KEYRING_BUILTIN``, ``_SECONDARY``, ``_PLATFORM`` for the system
    keyrings, ``_BPF`` for the bpf keyring, or ``_USER`` for a caller-supplied
    user/session keyring. It is ``BPF_SIG_KEYRING_NONE`` for an unsigned
    program.

``keyring_serial`` (``s32``)
    Set **only** on a successful verification, to the serial of the
    **caller-supplied key or keyring** that ``keyring_id`` resolved to - the
    object the signature was verified against, not the individual asymmetric
    key inside it that matched the signer. Passing
    ``KEY_SPEC_SESSION_KEYRING``, for example, records the session keyring's
    serial. The system keyrings and the bpf keyring are trusted as a whole and
    expose no serial here, so the serial is ``0`` for them, and ``0`` for
    unsigned programs. A non-zero ``keyring_serial`` is therefore exactly
    "verified against the caller-supplied key/keyring with this serial", which
    is exactly the case a policy has to scrutinise.

.. list-table::
   :header-rows: 1

   * - ``keyring_id``
     - ``keyring_type``
     - ``keyring_serial``
   * - (no signature)
     - ``BPF_SIG_KEYRING_NONE``
     - ``0``
   * - ``0``
     - ``BPF_SIG_KEYRING_BUILTIN``
     - ``0``
   * - ``VERIFY_USE_SECONDARY_KEYRING``
     - ``BPF_SIG_KEYRING_SECONDARY``
     - ``0``
   * - ``VERIFY_USE_PLATFORM_KEYRING``
     - ``BPF_SIG_KEYRING_PLATFORM``
     - ``0``
   * - ``KEY_SPEC_BPF_KEYRING``
     - ``BPF_SIG_KEYRING_BPF``
     - ``0``
   * - other (a caller-supplied key serial)
     - ``BPF_SIG_KEYRING_USER``
     - serial of the resolved key/keyring

Producing and loading a signed object
=====================================

Generating a signing key
------------------------

Signing is algorithm agnostic: the algorithm comes from the X.509 certificate
and the PKCS#7 ``SignerInfo``. Anything the X.509 and PKCS#7 parsers understand
works with no BPF-side change. Both examples below read the certificate request
config from ``x509.genkey``, for which ``certs/x509.genkey`` serves as a
template (see Documentation/admin-guide/module-signing.rst). RSA::

    openssl req -new -nodes -utf8 -sha256 -days 36500 -batch -x509 \
            -config x509.genkey -outform PEM \
            -out signing_key.pem -keyout signing_key.pem
    openssl x509 -in signing_key.pem -outform der -out signing_key.der

ML-DSA-87 (FIPS-204), which needs openssl 3.5 or later, and both
``CONFIG_CRYPTO_MLDSA`` and ``CONFIG_CRYPTO_SHA512`` in the kernel - the latter
for the signedAttrs digest described below, which ``CONFIG_CRYPTO_MLDSA`` does
not select. Note the absence of a digest option: ML-DSA hashes the message
itself, so openssl ignores an explicit digest for it::

    openssl req -new -nodes -utf8 -days 36500 -batch -x509 \
            -newkey ML-DSA-87 -config x509.genkey -outform PEM \
            -out signing_key.pem -keyout signing_key.pem
    openssl x509 -in signing_key.pem -outform der -out signing_key.der

``bpftool`` handles the following internally: openssl before 4.0 cannot combine
ML-DSA with ``CMS_NOATTR``, so it falls back to signedAttrs, where only SHA-512
is permitted. This mirrors what module signing does as well.

Signing
-------

``bpftool`` generates and signs a light skeleton in one step::

    bpftool gen skeleton -L -S -k signing_key.pem -i signing_key.der \
            obj.bpf.o > obj.lskel.h

``-L`` selects the light-skeleton (``gen_loader``) backend and ``-S`` enables
signing; ``-k`` and ``-i`` supply the signing key and its X.509 certificate.
``bpftool`` signs ``insns || metadata`` - the exact bytes the kernel
reconstructs - and also computes ``excl_prog_hash`` as the digest of the loader
instructions so the metadata map can be bound to the loader. The signature and
hash are embedded in the generated header; the certificate is used only for
signing and is not included.

Loading
-------

The generated skeleton exposes ``keyring_id``, which selects the keyring the
kernel verifies against. Set it between open and load; loading then performs
the create/populate/freeze/load sequence described above::

    struct obj *skel = obj__open();

    skel->keyring_id = KEY_SPEC_BPF_KEYRING;
    err = obj__load(skel);

For the staging case the same object is loaded against a keyring the caller
populated itself, which only works on a system that has not set
``bpf.keyring_unsealed=1``::

    /*
     * Staging only: this keyring is under the loader's own control and
     * carries no trust of its own. See "Caller-supplied keyrings are for
     * staging".
     */
    key_id = add_key("asymmetric", "", der, der_sz, KEY_SPEC_SESSION_KEYRING);
    skel->keyring_id = KEY_SPEC_SESSION_KEYRING;
    err = obj__load(skel);

Either way the trusted public key must already be in the chosen keyring for
verification to succeed. For the bpf keyring that enrollment happens once at
boot, see `Provisioning`_.

UAPI reference
==============

``BPF_PROG_LOAD`` (``union bpf_attr``):

``signature``, ``signature_size``
    Pointer to and length of the PKCS#7 signature blob.

``keyring_id``
    Trusted keyring selector (see `Keyrings`_).

``fd_array``, ``fd_array_cnt``
    Array of map (and module BTF) file descriptors bound to the program.
    ``fd_array_cnt`` must be set for the kernel to scan the array. When a
    signature is present, a BTF entry is rejected outright, and every map must
    be exclusive; its frozen contents are folded into the verified buffer, and
    a non-exclusive entry is rejected.

``BPF_MAP_CREATE`` (``union bpf_attr``):

``excl_prog_hash``, ``excl_prog_hash_size``
    SHA-256 digest of the program permitted to access this (exclusive) map. This
    binds the metadata map to the loader; it is not a hash of the map *content*.
    The map content is not hashed separately at all - it is covered, as bytes,
    by the program signature.

Kernel command line:

``bpf.keyring_unsealed=``
    Set to ``1`` to leave the bpf keyring unsealed for provisioning, and to make
    it the only non-system keyring a loader may select for the rest of the boot
    (see `The bpf keyring`_).

Notes and limitations
======================

- The instructions plus folded metadata are verified as one ``bpf_dynptr``,
  which bounds the combined size (currently ~16 MiB); very large objects can
  exceed it.
- The metadata container is a single-element array map, accessed through
  ``map_direct_value_addr``.
- The bpf keyring needs ``CONFIG_KEYS``; without it there is no bpf keyring
  and ``KEY_SPEC_BPF_KEYRING`` never resolves.
