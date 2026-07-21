Kernel Support for miscellaneous Binary Formats (binfmt_misc)
=============================================================

This Kernel feature allows you to invoke almost (for restrictions see below)
every program by simply typing its name in the shell.
This includes for example compiled Java(TM), Python or Emacs programs.

To achieve this you must tell binfmt_misc which interpreter has to be invoked
with which binary. Binfmt_misc recognises the binary-type by matching some bytes
at the beginning of the file with a magic byte sequence (masking out specified
bits) you have supplied. Binfmt_misc can also recognise a filename extension
aka ``.com`` or ``.exe``.

First you must mount binfmt_misc::

	mount binfmt_misc -t binfmt_misc /proc/sys/fs/binfmt_misc

To actually register a new binary type, you have to set up a string looking like
``:name:type:offset:magic:mask:interpreter:flags`` (where you can choose the
``:`` upon your needs) and echo it to ``/proc/sys/fs/binfmt_misc/register``.

Here is what the fields mean:

- ``name``
   is an identifier string. A new /proc file will be created with this
   name below ``/proc/sys/fs/binfmt_misc``; cannot contain slashes ``/`` for
   obvious reasons.
- ``type``
   is the type of recognition. Give ``M`` for magic, ``E`` for extension and
   ``B`` for a bpf-backed handler (see below).
- ``offset``
   is the offset of the magic/mask in the file, counted in bytes. This
   defaults to 0 if you omit it (i.e. you write ``:name:type::magic...``).
   Ignored when using filename extension matching.
- ``magic``
   is the byte sequence binfmt_misc is matching for. The magic string
   may contain hex-encoded characters like ``\x0a`` or ``\xA4``. Note that you
   must escape any NUL bytes; parsing halts at the first one. In a shell
   environment you might have to write ``\\x0a`` to prevent the shell from
   eating your ``\``.
   If you chose filename extension matching, this is the extension to be
   recognised (without the ``.``, the ``\x0a`` specials are not allowed).
   Extension    matching is case sensitive, and slashes ``/`` are not allowed!
- ``mask``
   is an (optional, defaults to all 0xff) mask. You can mask out some
   bits from matching by supplying a string like magic and as long as magic.
   The mask is anded with the byte sequence of the file. Note that you must
   escape any NUL bytes; parsing halts at the first one. Ignored when using
   filename extension matching.
- ``interpreter``
   is the program that should be invoked with the binary as first
   argument (specify the full path). For ``B`` entries this field
   carries the name of the bpf handler instead (see below).
- ``flags``
   is an optional field that controls several aspects of the invocation
   of the interpreter. It is a string of capital letters, each controls a
   certain aspect. The following flags are supported:

      ``P`` - preserve-argv[0]
            Legacy behavior of binfmt_misc is to overwrite
            the original argv[0] with the full path to the binary. When this
            flag is included, binfmt_misc will add an argument to the argument
            vector for this purpose, thus preserving the original ``argv[0]``.
            e.g. If your interp is set to ``/bin/foo`` and you run ``blah``
            (which is in ``/usr/local/bin``), then the kernel will execute
            ``/bin/foo`` with ``argv[]`` set to ``["/bin/foo", "/usr/local/bin/blah", "blah"]``.  The interp has to be aware of this so it can
            execute ``/usr/local/bin/blah``
            with ``argv[]`` set to ``["blah"]``.
      ``O`` - open-binary
	    Legacy behavior of binfmt_misc is to pass the full path
            of the binary to the interpreter as an argument. When this flag is
            included, binfmt_misc will open the file for reading and pass its
            descriptor into the auxilary vector with the key "AT_EXECFD", thus
            allowing the interpreter to execute non-readable binaries. This
            feature should be used with care - the interpreter has to be trusted
            not to emit the contents of the non-readable binary.
      ``C`` - credentials
            Currently, the behavior of binfmt_misc is to calculate
            the credentials and security token of the new process according to
            the interpreter. When this flag is included, these attributes are
            calculated according to the binary. It also implies the ``O`` flag.
            This feature should be used with care as the interpreter
            will run with root permissions when a setuid binary owned by root
            is run with binfmt_misc.
      ``F`` - fix binary
            The usual behaviour of binfmt_misc is to spawn the
	    binary lazily when the misc format file is invoked.  However,
	    this doesn't work very well in the face of mount namespaces and
	    changeroots, so the ``F`` mode opens the binary as soon as the
	    emulation is installed and uses the opened image to spawn the
	    emulator, meaning it is always available once installed,
	    regardless of how the environment changes.


There are some restrictions:

 - the whole register string may not exceed 1920 characters
 - the magic must reside in the first 128 bytes of the file, i.e.
   offset+size(magic) has to be less than 128
 - the interpreter string may not exceed 127 characters
 - an interpreter used with ``C`` but without ``F`` has to be named by an
   absolute path. It is opened when the binary is executed, so a relative
   one would be resolved against the working directory of whoever runs
   the binary


bpf-backed handlers
-------------------

With ``CONFIG_BINFMT_MISC_BPF`` both the matching and the interpreter
selection can be delegated to bpf programs. A handler is an instance of the
``binfmt_misc_ops`` struct_ops with a ``match`` and a ``load`` program and a
``name``. Once the struct_ops map is registered the handler can be activated
with a ``B`` entry that references it by name in the ``interpreter`` field
and carries neither offset, magic, nor mask::

    echo ':qemu:B::::my_handler:' > register

Both programs receive the ``linux_binprm`` of the binary and both can
sleep. The ``match`` program decides whether the handler applies: it is
consulted during the entry walk exactly like magic and extension matching,
in the same registration order with the same first-match-wins semantics.
Unlike static matching it is not limited to the prefetched first bytes of
the file in ``bprm->buf``: it can read the file, e.g. to parse ELF program
headers whose data sits at arbitrary offsets. It only decides, though: the
selection kfuncs below are rejected in it. The ``load`` program of the
matched handler then selects the interpreter: it can equally read the file
and derive the interpreter from the binary's location. It selects the
interpreter by calling the ``bpf_binprm_set_interp()`` kfunc with an
absolute path and returning ``0``. A match is committed: a failing
``load`` fails the exec with its error instead of falling through to later
entries; ``-ENOEXEC`` lets the remaining binary formats have a go. The
interpreter is opened with the credentials of the task doing the exec,
exactly as a statically registered interpreter would be.

The ``load`` program can also pass a single argument to the interpreter with
the ``bpf_binprm_set_interp_arg()`` kfunc. It is inserted between the
interpreter and the binary, exactly like the optional argument of a ``#!``
interpreter line, e.g. for a handler that resolves ``$ORIGIN`` in a script's
``#!`` path and needs to preserve the argument that followed it.

The invocation flags a static entry fixes at registration - ``P``, ``C``
and ``O`` - are per-exec choices for a bpf handler, made by the ``load``
program with the ``bpf_binprm_set_flags()`` kfunc, so a single handler can
decide them differently for each binary it handles:

- ``BPF_BINPRM_PRESERVE_ARGV0`` keeps the caller's ``argv[0]`` (the ``P``
  flag).
- ``BPF_BINPRM_CREDENTIALS`` computes credentials from the binary (the ``C``
  flag), bounded to user namespaces that map the binary's owner just like
  any other setuid exec.
- ``BPF_BINPRM_EXECFD`` opens the binary on the interpreter's behalf and
  passes it through the ``AT_EXECFD`` aux vector entry (the ``O`` flag), so
  the interpreter can run binaries it could not open by path.

Because these are program choices, a ``B`` entry carries no flags in the
register string; ``F`` (pre-open a fixed interpreter) has no meaning for it.

A handler is looked up only in the user namespace the struct_ops map was
registered in. Handlers are not inherited, so an entry can only reference a
handler registered in the same user namespace as its binfmt_misc instance.
The entry keeps the handler alive; deleting the struct_ops map only prevents
new activations.

To use binfmt_misc you have to mount it first. You can mount it with
``mount -t binfmt_misc none /proc/sys/fs/binfmt_misc`` command, or you can add
a line ``none  /proc/sys/fs/binfmt_misc binfmt_misc defaults 0 0`` to your
``/etc/fstab`` so it auto mounts on boot.

You may want to add the binary formats in one of your ``/etc/rc`` scripts during
boot-up. Read the manual of your init program to figure out how to do this
right.

Think about the order of adding entries! Later added entries are matched first!


A few examples (assumed you are in ``/proc/sys/fs/binfmt_misc``):

- enable support for em86 (like binfmt_em86, for Alpha AXP only)::

    echo ':i386:M::\x7fELF\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x03:\xff\xff\xff\xff\xff\xfe\xfe\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfb\xff\xff:/bin/em86:' > register
    echo ':i486:M::\x7fELF\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x06:\xff\xff\xff\xff\xff\xfe\xfe\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfb\xff\xff:/bin/em86:' > register

- enable support for packed DOS applications (pre-configured dosemu hdimages)::

    echo ':DEXE:M::\x0eDEX::/usr/bin/dosexec:' > register

- enable support for Windows executables using wine::

    echo ':DOSWin:M::MZ::/usr/local/bin/wine:' > register

For java support see Documentation/admin-guide/java.rst


You can enable/disable binfmt_misc or one binary type by echoing 0 (to disable)
or 1 (to enable) to ``/proc/sys/fs/binfmt_misc/status`` or
``/proc/.../the_name``.
Catting the file tells you the current status of ``binfmt_misc/the_entry``.

You can remove one entry or all entries by echoing -1 to ``/proc/.../the_name``
or ``/proc/sys/fs/binfmt_misc/status``. A single entry can also be removed
by simply unlinking (``rm``) ``/proc/.../the_name``.


Hints
-----

If you want to pass special arguments to your interpreter, you can
write a wrapper script for it.
See :doc:`Documentation/admin-guide/java.rst <./java>` for an example.

Your interpreter should NOT look in the PATH for the filename; the kernel
passes it the full filename (or the file descriptor) to use.  Using ``$PATH`` can
cause unexpected behaviour and can be a security hazard.


Richard Günther <rguenth@tat.physik.uni-tuebingen.de>
