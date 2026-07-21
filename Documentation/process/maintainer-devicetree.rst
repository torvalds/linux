.. SPDX-License-Identifier: GPL-2.0

======================================
Devicetree and Open Firmware Subsystem
======================================

Other Process Documents
-----------------------

Please see the documents in Documentation/devicetree/bindings/ for information
on how to write proper Devicetree bindings and how to submit patches.

Patch Review and Handling
-------------------------

Patches handled by Devicetree maintainers are processed differently depending
on the patch type:

1. Core OF driver code, e.g. drivers/of/:
   patches are reviewed and applied by DT maintainers.

2. Devicetree bindings:
   patches are reviewed by DT maintainers, but should be applied by subsystem
   maintainers except in certain cases.  See also *For kernel maintainers* in
   Documentation/devicetree/bindings/submitting-patches.rst.

3. DTS and drivers:
   DT maintainers might provide comments, but review is generally not expected.
   DTS must pass schema checks (dtbs_check) or at least do not add any
   new warnings.

Patchwork
~~~~~~~~~

Devicetree maintainers review patches using Patchwork, so the current status of
a patch can be checked there. For typical driver submissions, Patchwork
receives the entire patch set, but only a few patches are usually Devicetree
bindings that are reviewed by DT maintainers.

Explanation of Patchwork statuses:

 - **New**: Not yet processed by the automation toolset.
 - **Needs ACK**: Waiting for review by DT maintainers.
 - **Handled Elsewhere**: Non-DT patch; not being reviewed here.
 - **RFC**: Patch was likely ignored because it was an incomplete RFC.
 - **Changes Requested**: Patch was reviewed and DT maintainers expect changes.
 - **Accepted**: Patch was reviewed and applied by DT maintainers to their tree.
 - **Not Applicable**: Patch was reviewed and is likely in good shape, with a
   *Reviewed-by* or *Acked-by* tag provided, but DT maintainers expect someone
   else to apply it.

Patch Re-review and Pinging
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Due to the high volume of email traffic, Devicetree maintainers do not read
every email they receive and instead rely on Patchwork during the review
process. They also often skip patches that have already been reviewed.

As a result, maintainers might miss:

1. Questions about already reviewed patches.
2. Pings, for example when a patch has been reviewed by DT maintainers but has
   not been picked up by subsystem maintainers.

Such cases can be addressed by:

1. Pinging DT maintainers on the IRC channel.
2. Dropping the DT maintainer’s *Acked-by* or *Reviewed-by* tag when sending a new
   version of the patch set, together with an explanation in the patch
   changelog describing why the tag was removed and what is expected from DT
   maintainers.

