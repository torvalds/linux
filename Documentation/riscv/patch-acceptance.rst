.. SPDX-License-Identifier: GPL-2.0

arch/riscv maintenance guidelines for developers
================================================

Overview
--------
The RISC-V instruction set architecture is developed in the open:
in-progress drafts are available for all to review and to experiment
with implementations.  New module or extension drafts can change
during the development process - sometimes in ways that are
incompatible with previous drafts.  This flexibility can present a
challenge for RISC-V Linux maintenance.  Linux maintainers disapprove
of churn, and the Linux development process prefers well-reviewed and
tested code over experimental code.  We wish to extend these same
principles to the RISC-V-related code that will be accepted for
inclusion in the kernel.

Patchwork
---------

RISC-V has a patchwork instance, where the status of patches can be checked:

  https://patchwork.kernel.org/project/linux-riscv/list/

If your patch does not appear in the default view, the RISC-V maintainers have
likely either requested changes, or expect it to be applied to another tree.

Automation runs against this patchwork instance, building/testing patches as
they arrive. The automation applies patches against the current HEAD of the
RISC-V `for-next` and `fixes` branches, depending on whether the patch has been
detected as a fix. Failing those, it will use the RISC-V `master` branch.
The exact commit to which a series has been applied will be noted on patchwork.
Patches for which any of the checks fail are unlikely to be applied and in most
cases will need to be resubmitted.

Submit Checklist Addendum
-------------------------
We'll only accept patches for new modules or extensions if the
specifications for those modules or extensions are listed as being
unlikely to be incompatibly changed in the future.  For
specifications from the RISC-V foundation this means "Frozen" or
"Ratified", for the UEFI forum specifications this means a published
ECR.  (Developers may, of course, maintain their own Linux kernel trees
that contain code for any draft extensions that they wish.)

Additionally, the RISC-V specification allows implementers to create
their own custom extensions.  These custom extensions aren't required
to go through any review or ratification process by the RISC-V
Foundation.  To avoid the maintenance complexity and potential
performance impact of adding kernel code for implementor-specific
RISC-V extensions, we'll only consider patches for extensions that either:

- Have been officially frozen or ratified by the RISC-V Foundation, or
- Have been implemented in hardware that is widely available, per standard
  Linux practice.

(Implementers, may, of course, maintain their own Linux kernel trees containing
code for any custom extensions that they wish.)
