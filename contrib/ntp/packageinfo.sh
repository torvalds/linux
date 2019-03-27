#
# packageinfo.sh - set shell variables with version components
#
# This file is sourced by other scripts and does not need execute
# permission or the #! shell selector.
#
# Most changes to this file are fully or semi-automated using the
#   UpdatePoint script on the ntp.org tarball build machine.  Changes
#   required by the typical ntp.org release sequences are below.
#
## DEV:
#
# To bump the -dev point (p) number, UpdatePoint needs no changes here.
#
# To start a -RC cycle in -dev leading to the next -stable release,
#   set prerelease=rc.
#
# To move from dev -RC to new -stable and -dev major/minor version, set
#   minor and/or major to the new version, repotype to match the new
#   minor version, empty prerelease, and set point=NEW.  UpdatePoint
#   will empty point and rcpoint, and set betapoint=0.
#
## STABLE:
#
# To start a -stable beta cycle, which normally occurs before a -stable
#   -RC1 during the runup to a -stable point release, UpdatePoint needs
#   no changes here.  Both betapoint and point will be incremented, and
#   prerelease will be set to beta.
#
# To move on from -stable beta to RC set prerelease=rc.
#
# To fall back from -stable RC to beta set prerelease=beta.
#
# To skip over -stable beta1 directly to -RC1, set prerelease=rc.
#
# To skip all -stable prereleases and move from one primary or point 
#   release directly to the next point release, set rcpoint=GO.
#
##
#
# To see what UpdatePoint will do without modifying packageinfo.sh as it
# does by default, use the -t/--test option before the repo type:
#
# shell# scripts/build/UpdatePoint -t stable
#

# repotype must be stable or dev
repotype=stable

# post-4.2.8:
# version=Major.Minor
# 4.2.8 and before:
# version=Protocol.Major.Minor
# odd minor numbers are for -dev, even minor numbers are for -stable
# UpdatePoint will fail if repotype is inconsistent with minor.
proto=4
major=2
minor=8

case "${proto}.${major}" in
 4.[012])
    version=${proto}.${major}.${minor}
    ;;
 *) version=${major}.${minor}
    ;;
esac

# Special.  Normally unused.  A suffix.
#special=ag
special=

# prerelease can be empty, 'beta', or 'rc'.
prerelease=

# ChangeLog starting tag (see also CommitLog-4.1.0)
CLTAG=NTP_4_2_0

### post-4.2.8:
### Point number, after "major.minor.", normally modified by script.
### 4.2.8 and before:
### Point number, after "p", normally modified by script.
# 3 cases:
# - Numeric values increment
# - empty 'increments' to 1
# - NEW 'increments' to empty
point=13

### betapoint is normally modified by script.
# ntp-stable Beta number (betapoint)
# Should be zeroed at release, and left at zero until first beta.
# The first beta is -beta1.
# betapoint is always zero in ntp-dev.
betapoint=

### rcpoint is normally modified by script except for GO.
# RC number (rcpoint)
# for ntp-dev, always empty as RC numbers are not used, nor is GO.
# For ntp-stable:
# if prerelease is 'rc':
# - Numeric values increment
# - GO triggers a release
# - - rcpoint is emptied
# - - betapoint is set to 0
# - - prerelease is emptied
# else (not in RC)
# - rcpoint is empty and unused (for now).
rcpoint=
