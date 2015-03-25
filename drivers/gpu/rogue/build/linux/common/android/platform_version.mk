########################################################################### ###
#@Copyright     Copyright (c) Imagination Technologies Ltd. All Rights Reserved
#@License       Dual MIT/GPLv2
# 
# The contents of this file are subject to the MIT license as set out below.
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# Alternatively, the contents of this file may be used under the terms of
# the GNU General Public License Version 2 ("GPL") in which case the provisions
# of GPL are applicable instead of those above.
# 
# If you wish to allow use of your version of this file only under the terms of
# GPL, and not to allow others to use your version of this file under the terms
# of the MIT license, indicate your decision by deleting the provisions above
# and replace them with the notice and other provisions required by GPL as set
# out in the file called "GPL-COPYING" included in this distribution. If you do
# not delete the provisions above, a recipient may use your version of this file
# under the terms of either the MIT license or GPL.
# 
# This License is also included in this distribution in the file called
# "MIT-COPYING".
# 
# EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
# PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
### ###########################################################################

# If there's no build.prop file in the expected location, bail out. Tell the
# user which file we were trying to read in case TARGET_DEVICE was not set.
#
BUILD_PROP := $(TARGET_ROOT)/product/$(TARGET_DEVICE)/system/build.prop
ifeq ($(wildcard $(BUILD_PROP)),)
$(warning *** Could not determine Android version.  Did you set ANDROID_ROOT,\
OUT_DIR and TARGET_DEVICE in your environment correctly?)
$(error Error reading $(BUILD_PROP))
endif

# Extract version.release and version.codename from the build.prop file.
# If either of the values aren't in the build.prop, the Make variables won't
# be defined, and fallback handling will take place.
#
define newline


endef
$(eval $(subst #,$(newline),$(shell cat $(BUILD_PROP) | \
	grep '^ro.build.version.release=\|^ro.build.version.codename=' | \
	sed -e 's,ro.build.version.release=,PLATFORM_RELEASE=,' \
	    -e 's,ro.build.version.codename=,PLATFORM_CODENAME=,' | tr '\n' '#')))

define release-starts-with
$(shell echo $(PLATFORM_RELEASE) | grep -q ^$(1); \
	[ "$$?" = "0" ] && echo 1 || echo 0)
endef

# ro.build.version.release contains the version number for release builds, or
# the version codename otherwise. In this case we need to assume that the
# version of Android we're building against has the features that are in the
# final release of that version, so we set PLATFORM_RELEASE to the
# corresponding release number.
#
# NOTE: It's the _string_ ordering that matters here, not the version number
# ordering. You need to make sure that strings that are sub-strings of other
# checked strings appear _later_ in this list.
#
# e.g. 'KitKatMR' starts with 'KitKat', but it is not KitKat.
#
ifeq ($(call release-starts-with,JellyBeanMR1),1)
PLATFORM_RELEASE := 4.2
else ifeq ($(call release-starts-with,JellyBeanMR),1)
PLATFORM_RELEASE := 4.3
else ifeq ($(call release-starts-with,JellyBean),1)
PLATFORM_RELEASE := 4.1
else ifeq ($(call release-starts-with,KeyLimePie),1)
PLATFORM_RELEASE := 4.4
else ifeq ($(call release-starts-with,KitKatMR),1)
PLATFORM_RELEASE := 4.4.1
else ifeq ($(call release-starts-with,KitKat),1)
PLATFORM_RELEASE := 4.4
else ifeq ($(call release-starts-with,Lollipop),1)
PLATFORM_RELEASE := 5.0
else ifeq ($(PLATFORM_CODENAME),AOSP)
# AOSP (master) will normally have PLATFORM_CODENAME set to AOSP
PLATFORM_RELEASE := 5.1
else ifeq ($(shell echo $(PLATFORM_RELEASE) | grep -qE "[A-Za-z]+"; echo $$?),0)
PLATFORM_RELEASE := 5.1
endif

PLATFORM_RELEASE_MAJ   := $(shell echo $(PLATFORM_RELEASE) | cut -f1 -d'.')
PLATFORM_RELEASE_MIN   := $(shell echo $(PLATFORM_RELEASE) | cut -f2 -d'.')
PLATFORM_RELEASE_PATCH := $(shell echo $(PLATFORM_RELEASE) | cut -f3 -d'.')

# Not all versions have a patchlevel; fix that up here
#
ifeq ($(PLATFORM_RELEASE_PATCH),)
PLATFORM_RELEASE_PATCH := 0
endif

# Macros to help categorize support for features and API_LEVEL for tests.
#
is_at_least_jellybean_mr1 := \
	$(shell ( test $(PLATFORM_RELEASE_MAJ) -gt 4 || \
				( test $(PLATFORM_RELEASE_MAJ) -eq 4 && \
				  test $(PLATFORM_RELEASE_MIN) -ge 2 ) ) && echo 1 || echo 0)
is_at_least_jellybean_mr2 := \
	$(shell ( test $(PLATFORM_RELEASE_MAJ) -gt 4 || \
				( test $(PLATFORM_RELEASE_MAJ) -eq 4 && \
				  test $(PLATFORM_RELEASE_MIN) -ge 3 ) ) && echo 1 || echo 0)
is_at_least_kitkat := \
	$(shell ( test $(PLATFORM_RELEASE_MAJ) -gt 4 || \
				( test $(PLATFORM_RELEASE_MAJ) -eq 4 && \
				  test $(PLATFORM_RELEASE_MIN) -ge 4 ) ) && echo 1 || echo 0)
is_at_least_kitkat_mr1 := \
	$(shell ( test $(PLATFORM_RELEASE_MAJ) -gt 4 || \
				( test $(PLATFORM_RELEASE_MAJ) -eq 4 && \
				  test $(PLATFORM_RELEASE_MIN) -gt 4 ) || \
					( test $(PLATFORM_RELEASE_MAJ) -eq 4 && \
					  test $(PLATFORM_RELEASE_MIN) -eq 4 && \
					  test $(PLATFORM_RELEASE_PATCH) -ge 1 ) ) && echo 1 || echo 0)
is_at_least_lollipop := \
	$(shell ( test $(PLATFORM_RELEASE_MAJ) -ge 5 ) && echo 1 || echo 0)

# Assume "future versions" are >=5.1, but we don't really know
is_future_version := \
	$(shell ( test $(PLATFORM_RELEASE_MAJ) -gt 5 || \
				( test $(PLATFORM_RELEASE_MAJ) -eq 5 && \
				  test $(PLATFORM_RELEASE_MIN) -gt 0 ) ) && echo 1 || echo 0)

# Sometimes a feature is introduced in AOSP master that isn't in the current
# future version, but both versions are beyond our support level. This variable
# can be used to differentiate those builds.
#
ifeq ($(PLATFORM_CODENAME)$(is_future_version),AOSP1)
is_aosp_master := 1
else
is_aosp_master := 0
endif

# Picking an exact match of API_LEVEL for the platform we're building
# against can avoid compatibility theming and affords better integration.
#
ifeq ($(is_future_version),1)
# Temporarily pin to 19 until it is actually bumped to 20
API_LEVEL := 19
else ifeq ($(is_at_least_lollipop),1)
API_LEVEL := 21
#API_LEVEL := 20 was l-preview
else ifeq ($(is_at_least_kitkat),1)
API_LEVEL := 19
else ifeq ($(is_at_least_jellybean_mr2),1)
API_LEVEL := 18
else ifeq ($(is_at_least_jellybean_mr1),1)
API_LEVEL := 17
else
$(error Must build against Android >= 4.2)
endif

# Each DDK is tested against only a single version of the platform.
# Warn if a different platform version is used.
#
ifeq ($(is_future_version),1)
$(info WARNING: Android version is newer than this DDK supports)
else ifneq ($(is_at_least_kitkat),1)
$(info WARNING: Android version is older than this DDK supports)
endif
