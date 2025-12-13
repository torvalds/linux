.. SPDX-License-Identifier: GPL-2.0

=============
SoC Subsystem
=============

Overview
--------

The SoC subsystem is a place of aggregation for SoC-specific code.
The main components of the subsystem are:

* devicetrees (DTS) for 32- & 64-bit ARM and RISC-V
* 32-bit ARM board files (arch/arm/mach*)
* 32- & 64-bit ARM defconfigs
* SoC-specific drivers across architectures, in particular for 32- & 64-bit
  ARM, RISC-V and Loongarch

These "SoC-specific drivers" do not include clock, GPIO etc drivers that have
other top-level maintainers. The drivers/soc/ directory is generally meant
for kernel-internal drivers that are used by other drivers to provide SoC-
specific functionality like identifying an SoC revision or interfacing with
power domains.

The SoC subsystem also serves as an intermediate location for changes to
drivers/bus, drivers/firmware, drivers/reset and drivers/memory.  The addition
of new platforms, or the removal of existing ones, often go through the SoC
tree as a dedicated branch covering multiple subsystems.

The main SoC tree is housed on git.kernel.org:
  https://git.kernel.org/pub/scm/linux/kernel/git/soc/soc.git/

Maintainers
-----------

Clearly this is quite a wide range of topics, which no one person, or even
small group of people are capable of maintaining.  Instead, the SoC subsystem
is comprised of many submaintainers (platform maintainers), each taking care of
individual platforms and driver subdirectories.
In this regard, "platform" usually refers to a series of SoCs from a given
vendor, for example, Nvidia's series of Tegra SoCs.  Many submaintainers operate
on a vendor level, responsible for multiple product lines.  For several reasons,
including acquisitions/different business units in a company, things vary
significantly here.  The various submaintainers are documented in the
MAINTAINERS file.

Most of these submaintainers have their own trees where they stage patches,
sending pull requests to the main SoC tree.  These trees are usually, but not
always, listed in MAINTAINERS.

What the SoC tree is not, however, is a location for architecture-specific code
changes.  Each architecture has its own maintainers that are responsible for
architectural details, CPU errata and the like.

Submitting Patches for Given SoC
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All typical platform related patches should be sent via SoC submaintainers
(platform-specific maintainers).  This includes also changes to per-platform or
shared defconfigs (scripts/get_maintainer.pl might not provide correct
addresses in such case).

Submitting Patches to the Main SoC Maintainers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The main SoC maintainers can be reached via the alias soc@kernel.org only in
following cases:

1. There are no platform-specific maintainers.

2. Platform-specific maintainers are unresponsive.

3. Introducing a completely new SoC platform.  Such new SoC work should be sent
   first to common mailing lists, pointed out by scripts/get_maintainer.pl, for
   community review.  After positive community review, work should be sent to
   soc@kernel.org in one patchset containing new arch/foo/Kconfig entry, DTS
   files, MAINTAINERS file entry and optionally initial drivers with their
   Devicetree bindings.  The MAINTAINERS file entry should list new
   platform-specific maintainers, who are going to be responsible for handling
   patches for the platform from now on.

Note that the soc@kernel.org is usually not the place to discuss the patches,
thus work sent to this address should be already considered as acceptable by
the community.

Information for (new) Submaintainers
------------------------------------

As new platforms spring up, they often bring with them new submaintainers,
many of whom work for the silicon vendor, and may not be familiar with the
process.

Devicetree ABI Stability
~~~~~~~~~~~~~~~~~~~~~~~~

Perhaps one of the most important things to highlight is that dt-bindings
document the ABI between the devicetree and the kernel.
Please read Documentation/devicetree/bindings/ABI.rst.

If changes are being made to a DTS that are incompatible with old
kernels, the DTS patch should not be applied until the driver is, or an
appropriate time later.  Most importantly, any incompatible changes should be
clearly pointed out in the patch description and pull request, along with the
expected impact on existing users, such as bootloaders or other operating
systems.

Driver Branch Dependencies
~~~~~~~~~~~~~~~~~~~~~~~~~~

A common problem is synchronizing changes between device drivers and devicetree
files. Even if a change is compatible in both directions, this may require
coordinating how the changes get merged through different maintainer trees.

Usually the branch that includes a driver change will also include the
corresponding change to the devicetree binding description, to ensure they are
in fact compatible.  This means that the devicetree branch can end up causing
warnings in the "make dtbs_check" step.  If a devicetree change depends on
missing additions to a header file in include/dt-bindings/, it will fail the
"make dtbs" step and not get merged.

There are multiple ways to deal with this:

* Avoid defining custom macros in include/dt-bindings/ for hardware constants
  that can be derived from a datasheet -- binding macros in header files should
  only be used as a last resort if there is no natural way to define a binding

* Use literal values in the devicetree file in place of macros even when a
  header is required, and change them to the named representation in a
  following release

* Defer the devicetree changes to a release after the binding and driver have
  already been merged

* Change the bindings in a shared immutable branch that is used as the base for
  both the driver change and the devicetree changes

* Add duplicate defines in the devicetree file guarded by an #ifndef section,
  removing them in a later release

Devicetree Naming Convention
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The general naming scheme for devicetree files is as follows.  The aspects of a
platform that are set at the SoC level, like CPU cores, are contained in a file
named $soc.dtsi, for example, jh7100.dtsi.  Integration details, that will vary
from board to board, are described in $soc-$board.dts.  An example of this is
jh7100-beaglev-starlight.dts.  Often many boards are variations on a theme, and
frequently there are intermediate files, such as jh7100-common.dtsi, which sit
between the $soc.dtsi and $soc-$board.dts files, containing the descriptions of
common hardware.

Some platforms also have System on Modules, containing an SoC, which are then
integrated into several different boards. For these platforms, $soc-$som.dtsi
and $soc-$som-$board.dts are typical.

Directories are usually named after the vendor of the SoC at the time of its
inclusion, leading to some historical directory names in the tree.

Validating Devicetree Files
~~~~~~~~~~~~~~~~~~~~~~~~~~~

``make dtbs_check`` can be used to validate that devicetree files are compliant
with the dt-bindings that describe the ABI.  Please read the section
"Running checks" of Documentation/devicetree/bindings/writing-schema.rst for
more information on the validation of devicetrees.

For new platforms, or additions to existing ones, ``make dtbs_check`` should not
add any new warnings.  For RISC-V and Samsung SoC, ``make dtbs_check W=1`` is
required to not add any new warnings.
If in any doubt about a devicetree change, reach out to the devicetree
maintainers.

Branches and Pull Requests
~~~~~~~~~~~~~~~~~~~~~~~~~~

Just as the main SoC tree has several branches, it is expected that
submaintainers will do the same. Driver, defconfig and devicetree changes should
all be split into separate branches and appear in separate pull requests to the
SoC maintainers.  Each branch should be usable by itself and avoid
regressions that originate from dependencies on other branches.

Small sets of patches can also be sent as separate emails to soc@kernel.org,
grouped into the same categories.

If changes do not fit into the normal patterns, there can be additional
top-level branches, e.g. for a treewide rework, or the addition of new SoC
platforms including dts files and drivers.

Branches with a lot of changes can benefit from getting split up into separate
topics branches, even if they end up getting merged into the same branch of the
SoC tree.  An example here would be one branch for devicetree warning fixes, one
for a rework and one for newly added boards.

Another common way to split up changes is to send an early pull request with the
majority of the changes at some point between rc1 and rc4, following up with one
or more smaller pull requests towards the end of the cycle that can add late
changes or address problems identified while testing the first set.

While there is no cut-off time for late pull requests, it helps to only send
small branches as time gets closer to the merge window.

Pull requests for bugfixes for the current release can be sent at any time, but
again having multiple smaller branches is better than trying to combine too many
patches into one pull request.

The subject line of a pull request should begin with "[GIT PULL]" and made using
a signed tag, rather than a branch.  This tag should contain a short description
summarising the changes in the pull request.  For more detail on sending pull
requests, please see Documentation/maintainer/pull-requests.rst.
