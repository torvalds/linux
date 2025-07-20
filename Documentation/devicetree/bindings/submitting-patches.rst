.. SPDX-License-Identifier: GPL-2.0

==========================================
Submitting Devicetree (DT) binding patches
==========================================

I. For patch submitters
=======================

  0) Normal patch submission rules from
     Documentation/process/submitting-patches.rst applies.

  1) The Documentation/ and include/dt-bindings/ portion of the patch should
     be a separate patch. The preferred subject prefix for binding patches is::

       "dt-bindings: <binding dir>: ..."

     Few subsystems, like ASoC, media, regulators and SPI, expect reverse order
     of the prefixes::

       "<binding dir>: dt-bindings: ..."

     The 80 characters of the subject are precious. It is recommended to not
     use "Documentation" or "doc" because that is implied. All bindings are
     docs. Repeating "binding" again should also be avoided.

  2) DT binding files are written in DT schema format using json-schema
     vocabulary and YAML file format. The DT binding files must pass validation
     by running::

       make dt_binding_check

     See Documentation/devicetree/bindings/writing-schema.rst for more details
     about schema and tools setup.

  3) DT binding files should be dual licensed. The preferred license tag is
     (GPL-2.0-only OR BSD-2-Clause).

  4) Submit the entire series to the devicetree mailinglist at

       devicetree@vger.kernel.org

     and Cc: the DT maintainers. Use scripts/get_maintainer.pl to identify
     all of the DT maintainers.

  5) The Documentation/ portion of the patch should come in the series before
     the code implementing the binding.

  6) Any compatible strings used in a chip or board DTS file must be
     previously documented in the corresponding DT binding file
     in Documentation/devicetree/bindings.  This rule applies even if
     the Linux device driver does not yet match on the compatible
     string.  [ checkpatch will emit warnings if this step is not
     followed as of commit bff5da4335256513497cc8c79f9a9d1665e09864
     ("checkpatch: add DT compatible string documentation checks"). ]

  7) DTS is treated in general as driver-independent hardware description, thus
     any DTS patches, regardless whether using existing or new bindings, should
     be placed at the end of patchset to indicate no dependency of drivers on
     the DTS.  DTS will be anyway applied through separate tree or branch, so
     different order would indicate the serie is non-bisectable.

     If a driver subsystem maintainer prefers to apply entire set, instead of
     their relevant portion of patchset, please split the DTS patches into
     separate patchset with a reference in changelog or cover letter to the
     bindings submission on the mailing list.

  8) If a documented compatible string is not yet matched by the
     driver, the documentation should also include a compatible
     string that is matched by the driver.

  9) Bindings are actively used by multiple projects other than the Linux
     Kernel, extra care and consideration may need to be taken when making changes
     to existing bindings.

II. For kernel maintainers
==========================

  1) If you aren't comfortable reviewing a given binding, reply to it and ask
     the devicetree maintainers for guidance.  This will help them prioritize
     which ones to review and which ones are ok to let go.

  2) For driver (not subsystem) bindings: If you are comfortable with the
     binding, and it hasn't received an Acked-by from the devicetree
     maintainers after a few weeks, go ahead and take it.

     For subsystem bindings (anything affecting more than a single device),
     getting a devicetree maintainer to review it is required.

  3) For a series going though multiple trees, the binding patch should be
     kept with the driver using the binding.

  4) The DTS files should however never be applied via driver subsystem tree,
     but always via platform SoC trees on dedicated branches (see also
     Documentation/process/maintainer-soc.rst).

III. Notes
==========

  0) Please see Documentation/devicetree/bindings/ABI.rst for details
     regarding devicetree ABI.

  1) This document is intended as a general familiarization with the process as
     decided at the 2013 Kernel Summit.  When in doubt, the current word of the
     devicetree maintainers overrules this document.  In that situation, a patch
     updating this document would be appreciated.
