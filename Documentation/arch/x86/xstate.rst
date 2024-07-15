Using XSTATE features in user space applications
================================================

The x86 architecture supports floating-point extensions which are
enumerated via CPUID. Applications consult CPUID and use XGETBV to
evaluate which features have been enabled by the kernel XCR0.

Up to AVX-512 and PKRU states, these features are automatically enabled by
the kernel if available. Features like AMX TILE_DATA (XSTATE component 18)
are enabled by XCR0 as well, but the first use of related instruction is
trapped by the kernel because by default the required large XSTATE buffers
are not allocated automatically.

The purpose for dynamic features
--------------------------------

Legacy userspace libraries often have hard-coded, static sizes for
alternate signal stacks, often using MINSIGSTKSZ which is typically 2KB.
That stack must be able to store at *least* the signal frame that the
kernel sets up before jumping into the signal handler. That signal frame
must include an XSAVE buffer defined by the CPU.

However, that means that the size of signal stacks is dynamic, not static,
because different CPUs have differently-sized XSAVE buffers. A compiled-in
size of 2KB with existing applications is too small for new CPU features
like AMX. Instead of universally requiring larger stack, with the dynamic
enabling, the kernel can enforce userspace applications to have
properly-sized altstacks.

Using dynamically enabled XSTATE features in user space applications
--------------------------------------------------------------------

The kernel provides an arch_prctl(2) based mechanism for applications to
request the usage of such features. The arch_prctl(2) options related to
this are:

-ARCH_GET_XCOMP_SUPP

 arch_prctl(ARCH_GET_XCOMP_SUPP, &features);

 ARCH_GET_XCOMP_SUPP stores the supported features in userspace storage of
 type uint64_t. The second argument is a pointer to that storage.

-ARCH_GET_XCOMP_PERM

 arch_prctl(ARCH_GET_XCOMP_PERM, &features);

 ARCH_GET_XCOMP_PERM stores the features for which the userspace process
 has permission in userspace storage of type uint64_t. The second argument
 is a pointer to that storage.

-ARCH_REQ_XCOMP_PERM

 arch_prctl(ARCH_REQ_XCOMP_PERM, feature_nr);

 ARCH_REQ_XCOMP_PERM allows to request permission for a dynamically enabled
 feature or a feature set. A feature set can be mapped to a facility, e.g.
 AMX, and can require one or more XSTATE components to be enabled.

 The feature argument is the number of the highest XSTATE component which
 is required for a facility to work.

When requesting permission for a feature, the kernel checks the
availability. The kernel ensures that sigaltstacks in the process's tasks
are large enough to accommodate the resulting large signal frame. It
enforces this both during ARCH_REQ_XCOMP_SUPP and during any subsequent
sigaltstack(2) calls. If an installed sigaltstack is smaller than the
resulting sigframe size, ARCH_REQ_XCOMP_SUPP results in -ENOSUPP. Also,
sigaltstack(2) results in -ENOMEM if the requested altstack is too small
for the permitted features.

Permission, when granted, is valid per process. Permissions are inherited
on fork(2) and cleared on exec(3).

The first use of an instruction related to a dynamically enabled feature is
trapped by the kernel. The trap handler checks whether the process has
permission to use the feature. If the process has no permission then the
kernel sends SIGILL to the application. If the process has permission then
the handler allocates a larger xstate buffer for the task so the large
state can be context switched. In the unlikely cases that the allocation
fails, the kernel sends SIGSEGV.

AMX TILE_DATA enabling example
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Below is the example of how userspace applications enable
TILE_DATA dynamically:

  1. The application first needs to query the kernel for AMX
     support::

        #include <asm/prctl.h>
        #include <sys/syscall.h>
        #include <stdio.h>
        #include <unistd.h>

        #ifndef ARCH_GET_XCOMP_SUPP
        #define ARCH_GET_XCOMP_SUPP  0x1021
        #endif

        #ifndef ARCH_XCOMP_TILECFG
        #define ARCH_XCOMP_TILECFG   17
        #endif

        #ifndef ARCH_XCOMP_TILEDATA
        #define ARCH_XCOMP_TILEDATA  18
        #endif

        #define MASK_XCOMP_TILE      ((1 << ARCH_XCOMP_TILECFG) | \
                                      (1 << ARCH_XCOMP_TILEDATA))

        unsigned long features;
        long rc;

        ...

        rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_SUPP, &features);

        if (!rc && (features & MASK_XCOMP_TILE) == MASK_XCOMP_TILE)
            printf("AMX is available.\n");

  2. After that, determining support for AMX, an application must
     explicitly ask permission to use it::

        #ifndef ARCH_REQ_XCOMP_PERM
        #define ARCH_REQ_XCOMP_PERM  0x1023
        #endif

        ...

        rc = syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, ARCH_XCOMP_TILEDATA);

        if (!rc)
            printf("AMX is ready for use.\n");

Note this example does not include the sigaltstack preparation.

Dynamic features in signal frames
---------------------------------

Dynamically enabled features are not written to the signal frame upon signal
entry if the feature is in its initial configuration.  This differs from
non-dynamic features which are always written regardless of their
configuration.  Signal handlers can examine the XSAVE buffer's XSTATE_BV
field to determine if a features was written.

Dynamic features for virtual machines
-------------------------------------

The permission for the guest state component needs to be managed separately
from the host, as they are exclusive to each other. A coupled of options
are extended to control the guest permission:

-ARCH_GET_XCOMP_GUEST_PERM

 arch_prctl(ARCH_GET_XCOMP_GUEST_PERM, &features);

 ARCH_GET_XCOMP_GUEST_PERM is a variant of ARCH_GET_XCOMP_PERM. So it
 provides the same semantics and functionality but for the guest
 components.

-ARCH_REQ_XCOMP_GUEST_PERM

 arch_prctl(ARCH_REQ_XCOMP_GUEST_PERM, feature_nr);

 ARCH_REQ_XCOMP_GUEST_PERM is a variant of ARCH_REQ_XCOMP_PERM. It has the
 same semantics for the guest permission. While providing a similar
 functionality, this comes with a constraint. Permission is frozen when the
 first VCPU is created. Any attempt to change permission after that point
 is going to be rejected. So, the permission has to be requested before the
 first VCPU creation.

Note that some VMMs may have already established a set of supported state
components. These options are not presumed to support any particular VMM.
