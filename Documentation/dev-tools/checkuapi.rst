.. SPDX-License-Identifier: GPL-2.0-only

============
UAPI Checker
============

The UAPI checker (``scripts/check-uapi.sh``) is a shell script which
checks UAPI header files for userspace backwards-compatibility across
the git tree.

Options
=======

This section will describe the options with which ``check-uapi.sh``
can be run.

Usage::

    check-uapi.sh [-b BASE_REF] [-p PAST_REF] [-j N] [-l ERROR_LOG] [-i] [-q] [-v]

Available options::

    -b BASE_REF    Base git reference to use for comparison. If unspecified or empty,
                   will use any dirty changes in tree to UAPI files. If there are no
                   dirty changes, HEAD will be used.
    -p PAST_REF    Compare BASE_REF to PAST_REF (e.g. -p v6.1). If unspecified or empty,
                   will use BASE_REF^1. Must be an ancestor of BASE_REF. Only headers
                   that exist on PAST_REF will be checked for compatibility.
    -j JOBS        Number of checks to run in parallel (default: number of CPU cores).
    -l ERROR_LOG   Write error log to file (default: no error log is generated).
    -i             Ignore ambiguous changes that may or may not break UAPI compatibility.
    -q             Quiet operation.
    -v             Verbose operation (print more information about each header being checked).

Environmental args::

    ABIDIFF  Custom path to abidiff binary
    CC       C compiler (default is "gcc")
    ARCH     Target architecture of C compiler (default is host arch)

Exit codes::

    0) Success
    1) ABI difference detected
    2) Prerequisite not met

Examples
========

Basic Usage
-----------

First, let's try making a change to a UAPI header file that obviously
won't break userspace::

    cat << 'EOF' | patch -l -p1
    --- a/include/uapi/linux/acct.h
    +++ b/include/uapi/linux/acct.h
    @@ -21,7 +21,9 @@
     #include <asm/param.h>
     #include <asm/byteorder.h>

    -/*
    +#define FOO
    +
    +/*
      *  comp_t is a 16-bit "floating" point number with a 3-bit base 8
      *  exponent and a 13-bit fraction.
      *  comp2_t is 24-bit with 5-bit base 2 exponent and 20 bit fraction
    diff --git a/include/uapi/linux/bpf.h b/include/uapi/linux/bpf.h
    EOF

Now, let's use the script to validate::

    % ./scripts/check-uapi.sh
    Installing user-facing UAPI headers from dirty tree... OK
    Installing user-facing UAPI headers from HEAD... OK
    Checking changes to UAPI headers between HEAD and dirty tree...
    All 912 UAPI headers compatible with x86 appear to be backwards compatible

Let's add another change that *might* break userspace::

    cat << 'EOF' | patch -l -p1
    --- a/include/uapi/linux/bpf.h
    +++ b/include/uapi/linux/bpf.h
    @@ -74,7 +74,7 @@ struct bpf_insn {
            __u8    dst_reg:4;      /* dest register */
            __u8    src_reg:4;      /* source register */
            __s16   off;            /* signed offset */
    -       __s32   imm;            /* signed immediate constant */
    +       __u32   imm;            /* unsigned immediate constant */
     };

     /* Key of an a BPF_MAP_TYPE_LPM_TRIE entry */
    EOF

The script will catch this::

    % ./scripts/check-uapi.sh
    Installing user-facing UAPI headers from dirty tree... OK
    Installing user-facing UAPI headers from HEAD... OK
    Checking changes to UAPI headers between HEAD and dirty tree...
    ==== ABI differences detected in include/linux/bpf.h from HEAD -> dirty tree ====
        [C] 'struct bpf_insn' changed:
          type size hasn't changed
          1 data member change:
            type of '__s32 imm' changed:
              typedef name changed from __s32 to __u32 at int-ll64.h:27:1
              underlying type 'int' changed:
                type name changed from 'int' to 'unsigned int'
                type size hasn't changed
    ==================================================================================

    error - 1/912 UAPI headers compatible with x86 appear _not_ to be backwards compatible

In this case, the script is reporting the type change because it could
break a userspace program that passes in a negative number. Now, let's
say you know that no userspace program could possibly be using a negative
value in ``imm``, so changing to an unsigned type there shouldn't hurt
anything. You can pass the ``-i`` flag to the script to ignore changes
in which the userspace backwards compatibility is ambiguous::

    % ./scripts/check-uapi.sh -i
    Installing user-facing UAPI headers from dirty tree... OK
    Installing user-facing UAPI headers from HEAD... OK
    Checking changes to UAPI headers between HEAD and dirty tree...
    All 912 UAPI headers compatible with x86 appear to be backwards compatible

Now, let's make a similar change that *will* break userspace::

    cat << 'EOF' | patch -l -p1
    --- a/include/uapi/linux/bpf.h
    +++ b/include/uapi/linux/bpf.h
    @@ -71,8 +71,8 @@ enum {

     struct bpf_insn {
            __u8    code;           /* opcode */
    -       __u8    dst_reg:4;      /* dest register */
            __u8    src_reg:4;      /* source register */
    +       __u8    dst_reg:4;      /* dest register */
            __s16   off;            /* signed offset */
            __s32   imm;            /* signed immediate constant */
     };
    EOF

Since we're re-ordering an existing struct member, there's no ambiguity,
and the script will report the breakage even if you pass ``-i``::

    % ./scripts/check-uapi.sh -i
    Installing user-facing UAPI headers from dirty tree... OK
    Installing user-facing UAPI headers from HEAD... OK
    Checking changes to UAPI headers between HEAD and dirty tree...
    ==== ABI differences detected in include/linux/bpf.h from HEAD -> dirty tree ====
        [C] 'struct bpf_insn' changed:
          type size hasn't changed
          2 data member changes:
            '__u8 dst_reg' offset changed from 8 to 12 (in bits) (by +4 bits)
            '__u8 src_reg' offset changed from 12 to 8 (in bits) (by -4 bits)
    ==================================================================================

    error - 1/912 UAPI headers compatible with x86 appear _not_ to be backwards compatible

Let's commit the breaking change, then commit the innocuous change::

    % git commit -m 'Breaking UAPI change' include/uapi/linux/bpf.h
    [detached HEAD f758e574663a] Breaking UAPI change
     1 file changed, 1 insertion(+), 1 deletion(-)
    % git commit -m 'Innocuous UAPI change' include/uapi/linux/acct.h
    [detached HEAD 2e87df769081] Innocuous UAPI change
     1 file changed, 3 insertions(+), 1 deletion(-)

Now, let's run the script again with no arguments::

    % ./scripts/check-uapi.sh
    Installing user-facing UAPI headers from HEAD... OK
    Installing user-facing UAPI headers from HEAD^1... OK
    Checking changes to UAPI headers between HEAD^1 and HEAD...
    All 912 UAPI headers compatible with x86 appear to be backwards compatible

It doesn't catch any breaking change because, by default, it only
compares ``HEAD`` to ``HEAD^1``. The breaking change was committed on
``HEAD~2``. If we wanted the search scope to go back further, we'd have to
use the ``-p`` option to pass a different past reference. In this case,
let's pass ``-p HEAD~2`` to the script so it checks UAPI changes between
``HEAD~2`` and ``HEAD``::

    % ./scripts/check-uapi.sh -p HEAD~2
    Installing user-facing UAPI headers from HEAD... OK
    Installing user-facing UAPI headers from HEAD~2... OK
    Checking changes to UAPI headers between HEAD~2 and HEAD...
    ==== ABI differences detected in include/linux/bpf.h from HEAD~2 -> HEAD ====
        [C] 'struct bpf_insn' changed:
          type size hasn't changed
          2 data member changes:
            '__u8 dst_reg' offset changed from 8 to 12 (in bits) (by +4 bits)
            '__u8 src_reg' offset changed from 12 to 8 (in bits) (by -4 bits)
    ==============================================================================

    error - 1/912 UAPI headers compatible with x86 appear _not_ to be backwards compatible

Alternatively, we could have also run with ``-b HEAD~``. This would set the
base reference to ``HEAD~`` so then the script would compare it to ``HEAD~^1``.

Architecture-specific Headers
-----------------------------

Consider this change::

    cat << 'EOF' | patch -l -p1
    --- a/arch/arm64/include/uapi/asm/sigcontext.h
    +++ b/arch/arm64/include/uapi/asm/sigcontext.h
    @@ -70,6 +70,7 @@ struct sigcontext {
     struct _aarch64_ctx {
            __u32 magic;
            __u32 size;
    +       __u32 new_var;
     };

     #define FPSIMD_MAGIC   0x46508001
    EOF

This is a change to an arm64-specific UAPI header file. In this example, I'm
running the script from an x86 machine with an x86 compiler, so, by default,
the script only checks x86-compatible UAPI header files::

    % ./scripts/check-uapi.sh
    Installing user-facing UAPI headers from dirty tree... OK
    Installing user-facing UAPI headers from HEAD... OK
    No changes to UAPI headers were applied between HEAD and dirty tree

With an x86 compiler, we can't check header files in ``arch/arm64``, so the
script doesn't even try.

If we want to check the header file, we'll have to use an arm64 compiler and
set ``ARCH`` accordingly::

    % CC=aarch64-linux-gnu-gcc ARCH=arm64 ./scripts/check-uapi.sh
    Installing user-facing UAPI headers from dirty tree... OK
    Installing user-facing UAPI headers from HEAD... OK
    Checking changes to UAPI headers between HEAD and dirty tree...
    ==== ABI differences detected in include/asm/sigcontext.h from HEAD -> dirty tree ====
        [C] 'struct _aarch64_ctx' changed:
          type size changed from 64 to 96 (in bits)
          1 data member insertion:
            '__u32 new_var', at offset 64 (in bits) at sigcontext.h:73:1
        -- snip --
        [C] 'struct zt_context' changed:
          type size changed from 128 to 160 (in bits)
          2 data member changes (1 filtered):
            '__u16 nregs' offset changed from 64 to 96 (in bits) (by +32 bits)
            '__u16 __reserved[3]' offset changed from 80 to 112 (in bits) (by +32 bits)
    =======================================================================================

    error - 1/884 UAPI headers compatible with arm64 appear _not_ to be backwards compatible

We can see with ``ARCH`` and ``CC`` set properly for the file, the ABI
change is reported properly. Also notice that the total number of UAPI
header files checked by the script changes. This is because the number
of headers installed for arm64 platforms is different than x86.

Cross-Dependency Breakages
--------------------------

Consider this change::

    cat << 'EOF' | patch -l -p1
    --- a/include/uapi/linux/types.h
    +++ b/include/uapi/linux/types.h
    @@ -52,7 +52,7 @@ typedef __u32 __bitwise __wsum;
     #define __aligned_be64 __be64 __attribute__((aligned(8)))
     #define __aligned_le64 __le64 __attribute__((aligned(8)))

    -typedef unsigned __bitwise __poll_t;
    +typedef unsigned short __bitwise __poll_t;

     #endif /*  __ASSEMBLY__ */
     #endif /* _UAPI_LINUX_TYPES_H */
    EOF

Here, we're changing a ``typedef`` in ``types.h``. This doesn't break
a UAPI in ``types.h``, but other UAPIs in the tree may break due to
this change::

    % ./scripts/check-uapi.sh
    Installing user-facing UAPI headers from dirty tree... OK
    Installing user-facing UAPI headers from HEAD... OK
    Checking changes to UAPI headers between HEAD and dirty tree...
    ==== ABI differences detected in include/linux/eventpoll.h from HEAD -> dirty tree ====
        [C] 'struct epoll_event' changed:
          type size changed from 96 to 80 (in bits)
          2 data member changes:
            type of '__poll_t events' changed:
              underlying type 'unsigned int' changed:
                type name changed from 'unsigned int' to 'unsigned short int'
                type size changed from 32 to 16 (in bits)
            '__u64 data' offset changed from 32 to 16 (in bits) (by -16 bits)
    ========================================================================================
    include/linux/eventpoll.h did not change between HEAD and dirty tree...
    It's possible a change to one of the headers it includes caused this error:
    #include <linux/fcntl.h>
    #include <linux/types.h>

Note that the script noticed the failing header file did not change,
so it assumes one of its includes must have caused the breakage. Indeed,
we can see ``linux/types.h`` is used from ``eventpoll.h``.

UAPI Header Removals
--------------------

Consider this change::

    cat << 'EOF' | patch -l -p1
    diff --git a/include/uapi/asm-generic/Kbuild b/include/uapi/asm-generic/Kbuild
    index ebb180aac74e..a9c88b0a8b3b 100644
    --- a/include/uapi/asm-generic/Kbuild
    +++ b/include/uapi/asm-generic/Kbuild
    @@ -31,6 +31,6 @@ mandatory-y += stat.h
     mandatory-y += statfs.h
     mandatory-y += swab.h
     mandatory-y += termbits.h
    -mandatory-y += termios.h
    +#mandatory-y += termios.h
     mandatory-y += types.h
     mandatory-y += unistd.h
    EOF

This script removes a UAPI header file from the install list. Let's run
the script::

    % ./scripts/check-uapi.sh
    Installing user-facing UAPI headers from dirty tree... OK
    Installing user-facing UAPI headers from HEAD... OK
    Checking changes to UAPI headers between HEAD and dirty tree...
    ==== UAPI header include/asm/termios.h was removed between HEAD and dirty tree ====

    error - 1/912 UAPI headers compatible with x86 appear _not_ to be backwards compatible

Removing a UAPI header is considered a breaking change, and the script
will flag it as such.

Checking Historic UAPI Compatibility
------------------------------------

You can use the ``-b`` and ``-p`` options to examine different chunks of your
git tree. For example, to check all changed UAPI header files between tags
v6.0 and v6.1, you'd run::

    % ./scripts/check-uapi.sh -b v6.1 -p v6.0
    Installing user-facing UAPI headers from v6.1... OK
    Installing user-facing UAPI headers from v6.0... OK
    Checking changes to UAPI headers between v6.0 and v6.1...

    --- snip ---
    error - 37/907 UAPI headers compatible with x86 appear _not_ to be backwards compatible

Note: Before v5.3, a header file needed by the script is not present,
so the script is unable to check changes before then.

You'll notice that the script detected many UAPI changes that are not
backwards compatible. Knowing that kernel UAPIs are supposed to be stable
forever, this is an alarming result. This brings us to the next section:
caveats.

Caveats
=======

The UAPI checker makes no assumptions about the author's intention, so some
types of changes may be flagged even though they intentionally break UAPI.

Removals For Refactoring or Deprecation
---------------------------------------

Sometimes drivers for very old hardware are removed, such as in this example::

    % ./scripts/check-uapi.sh -b ba47652ba655
    Installing user-facing UAPI headers from ba47652ba655... OK
    Installing user-facing UAPI headers from ba47652ba655^1... OK
    Checking changes to UAPI headers between ba47652ba655^1 and ba47652ba655...
    ==== UAPI header include/linux/meye.h was removed between ba47652ba655^1 and ba47652ba655 ====

    error - 1/910 UAPI headers compatible with x86 appear _not_ to be backwards compatible

The script will always flag removals (even if they're intentional).

Struct Expansions
-----------------

Depending on how a structure is handled in kernelspace, a change which
expands a struct could be non-breaking.

If a struct is used as the argument to an ioctl, then the kernel driver
must be able to handle ioctl commands of any size. Beyond that, you need
to be careful when copying data from the user. Say, for example, that
``struct foo`` is changed like this::

    struct foo {
        __u64 a; /* added in version 1 */
    +   __u32 b; /* added in version 2 */
    +   __u32 c; /* added in version 2 */
    }

By default, the script will flag this kind of change for further review::

    [C] 'struct foo' changed:
      type size changed from 64 to 128 (in bits)
      2 data member insertions:
        '__u32 b', at offset 64 (in bits)
        '__u32 c', at offset 96 (in bits)

However, it is possible that this change was made safely.

If a userspace program was built with version 1, it will think
``sizeof(struct foo)`` is 8. That size will be encoded in the
ioctl value that gets sent to the kernel. If the kernel is built
with version 2, it will think the ``sizeof(struct foo)`` is 16.

The kernel can use the ``_IOC_SIZE`` macro to get the size encoded
in the ioctl code that the user passed in and then use
``copy_struct_from_user()`` to safely copy the value::

    int handle_ioctl(unsigned long cmd, unsigned long arg)
    {
        switch _IOC_NR(cmd) {
        0x01: {
            struct foo my_cmd;  /* size 16 in the kernel */

            ret = copy_struct_from_user(&my_cmd, arg, sizeof(struct foo), _IOC_SIZE(cmd));
            ...

``copy_struct_from_user`` will zero the struct in the kernel and then copy
only the bytes passed in from the user (leaving new members zeroized).
If the user passed in a larger struct, the extra members are ignored.

If you know this situation is accounted for in the kernel code, you can
pass ``-i`` to the script, and struct expansions like this will be ignored.

Flex Array Migration
--------------------

While the script handles expansion into an existing flex array, it does
still flag initial migration to flex arrays from 1-element fake flex
arrays. For example::

    struct foo {
          __u32 x;
    -     __u32 flex[1]; /* fake flex */
    +     __u32 flex[];  /* real flex */
    };

This change would be flagged by the script::

    [C] 'struct foo' changed:
      type size changed from 64 to 32 (in bits)
      1 data member change:
        type of '__u32 flex[1]' changed:
          type name changed from '__u32[1]' to '__u32[]'
          array type size changed from 32 to 'unknown'
          array type subrange 1 changed length from 1 to 'unknown'

At this time, there's no way to filter these types of changes, so be
aware of this possible false positive.

Summary
-------

While many types of false positives are filtered out by the script,
it's possible there are some cases where the script flags a change
which does not break UAPI. It's also possible a change which *does*
break userspace would not be flagged by this script. While the script
has been run on much of the kernel history, there could still be corner
cases that are not accounted for.

The intention is for this script to be used as a quick check for
maintainers or automated tooling, not as the end-all authority on
patch compatibility. It's best to remember: use your best judgment
(and ideally a unit test in userspace) to make sure your UAPI changes
are backwards-compatible!
