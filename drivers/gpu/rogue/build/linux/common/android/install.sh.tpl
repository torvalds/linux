#!/bin/bash
############################################################################ ###
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
#### ###########################################################################
# Help on how to invoke
#
function usage {
    echo "usage: $0 [options...]"
    echo ""
    echo "Options: -v            Verbose mode."
    echo "         -n            Dry-run mode."
    echo "         -u            Uninstall-only mode."
    echo "         --root <path> Use <path> as the root of the install file system."
    echo "                       (Overrides the DISCIMAGE environment variable.)"
    exit 1
}

WD=`pwd`
SCRIPT_ROOT=`dirname $0`
cd $SCRIPT_ROOT

PVRVERSION=[PVRVERSION]
PVRBUILD=[PVRBUILD]
PRIMARY_ARCH="[PRIMARY_ARCH]"
ARCHITECTURES="[ARCHITECTURES]"

# These destination directories are the same for 32- or 64-bit binaries.
# zxl: change module's path from /system/modules/ to /system/lib/modules/
MOD_DESTDIR=/system/lib/modules
BIN_DESTDIR=/system/vendor/bin
DATA_DESTDIR=${BIN_DESTDIR}

# Exit with an error messages.
# $1=blurb
#
function bail {
    if [ ! -z "$1" ]; then
        echo "$1" >&2
    fi

    echo "" >&2
    echo "Installation failed" >&2
    exit 1
}

# Copy all the required files into their appropriate places on the local machine.
function install_locally {
    # Define functions required for local installs

    # basic installation function
    # $1=fromfile, $2=destfilename, $3=blurb, $4=chmod-flags, $5=chown-flags
    #
    function install_file {
        if [ -z "$DDK_INSTALL_LOG" ]; then
            bail "INTERNAL ERROR: Invoking install without setting logfile name"
        fi
        DESTFILE=${DISCIMAGE}$2
        DESTDIR=`dirname $DESTFILE`
    
        if [ ! -e $1 ]; then
            [ -n "$VERBOSE" ] && echo "skipping file $1 -> $2"
            return
        fi
        
        # Destination directory - make sure it's there and writable
        #
        if [ -d "${DESTDIR}" ]; then
            if [ ! -w "${DESTDIR}" ]; then
                bail "${DESTDIR} is not writable."
            fi
        else
            $DOIT mkdir -p ${DESTDIR} || bail "Couldn't mkdir -p ${DESTDIR}"
            [ -n "$VERBOSE" ] && echo "Created directory `dirname $2`"
        fi
    
        # Delete the original so that permissions don't persist.
        #
        $DOIT rm -f $DESTFILE
    
        $DOIT cp -f $1 $DESTFILE || bail "Couldn't copy $1 to $DESTFILE"
        $DOIT chmod $4 ${DESTFILE}
    
        echo "$3 `basename $1` -> $2"
        $DOIT echo "file $2" >> $DDK_INSTALL_LOG
    }

	# Android-specific targetfs mkdir workarounds
	if [ ! -d ${DISCIMAGE}/data ]; then
		mkdir ${DISCIMAGE}/data
		chown 1000:1000 ${DISCIMAGE}/data
		chmod 0771 ${DISCIMAGE}/data
	fi
	if [ ! -d ${DISCIMAGE}/data/app ]; then
		mkdir ${DISCIMAGE}/data/app
		chown 1000:1000 ${DISCIMAGE}/data/app
		chmod 0771 ${DISCIMAGE}/data/app
	fi

    for arch in $ARCHITECTURES; do
        if [ ! -d $arch ]; then
            echo "Unknown architecture $arch.  Aborting"
            #exit 1
        fi

        case $arch in
            target*64)
                SHLIB_DESTDIR=/system/vendor/lib64
                ;;
            *)
                SHLIB_DESTDIR=/system/vendor/lib
        esac
        EGL_DESTDIR=${SHLIB_DESTDIR}/egl

        pushd $arch > /dev/null
        # Install UM components
        if [ -f install_um.sh ]; then
            DDK_INSTALL_LOG=$UMLOG
            echo "Installing User components for architecture $arch"
            $DOIT echo "version $PVRVERSION" > $DDK_INSTALL_LOG
            source install_um.sh
            echo 
        fi
        popd > /dev/null
    done

    pushd $PRIMARY_ARCH > /dev/null
    # Install KM components
    if [ -f install_km.sh ]; then
        DDK_INSTALL_LOG=$KMLOG
        echo "Installing Kernel components for architecture $PRIMARY_ARCH"
        $DOIT echo "version $PVRVERSION" > $DDK_INSTALL_LOG
        source install_km.sh
        echo
    fi
    popd > /dev/null

	$DOIT mkdir -p ${DISCIMAGE}/system/lib/egl
	$DOIT cat >${DISCIMAGE}/system/lib/egl/egl.cfg <<EOF
0 0 POWERVR_ROGUE
EOF
	$DOIT echo "file /system/lib/egl/egl.cfg" >> $DDK_INSTALL_LOG

    # Create an OLDLOG so old versions of the driver can uninstall.
    $DOIT echo "version $PVRVERSION" > $OLDLOG
    if [ -f $KMLOG ]; then
        tail -n +2 $KMLOG >> $OLDLOG
    fi
    if [ -f $UMLOG ]; then
        tail -n +2 $UMLOG >> $OLDLOG
    fi
    
    # Make sure new logs are newer than $OLDLOG
    touch -m -d "last sunday" $OLDLOG
}

# Read the appropriate install log and delete anything therein.
function uninstall_locally {
    # Function to uninstall something.
    function do_uninstall {
        LOG=$1

        if [ ! -f $LOG ]; then
            echo "Nothing to un-install."
            return;
        fi
    
        BAD=0
        VERSION=""
        while read type data; do
            case $type in
            version)
                echo "Uninstalling existing version $data"
                VERSION="$data"
                ;;
            link|file) 
                if [ -z "$VERSION" ]; then
                    BAD=1;
                    echo "No version record at head of $LOG"
                elif ! $DOIT rm -f ${DISCIMAGE}${data}; then
                    BAD=1;
                else
                    [ -n "$VERBOSE" ] && echo "Deleted $type $data"
                fi
                ;;
            tree)
                ;;
            esac
        done < $1;

        if [ $BAD = 0 ]; then
            echo "Uninstallation completed."
            $DOIT rm -f $LOG
        else
            echo "Uninstallation failed!!!"
        fi
    }

    if [ -z "$OLDLOG" -o -z "$KMLOG" -o -z "$UMLOG" ]; then
        bail "INTERNAL ERROR: Invoking uninstall without setting logfile name"
    fi

    # Uninstall anything installed using the old-style install scripts.
    LEGACY_LOG=0
    if [ -f $OLDLOG ]; then
        if [ -f $KMLOG -a $KMLOG -nt $OLDLOG ]; then
            # Last install was new scheme.
            rm $OLDLOG
        elif [ -f $UMLOG -a $UMLOG -nt $OLDLOG ]; then
            # Last install was new scheme.
            rm $OLDLOG
        else
            echo "Uninstalling all components from legacy log."
            do_uninstall $OLDLOG
            LEGACY_LOG=1
            echo 
        fi
    fi

    if [ $LEGACY_LOG = 0 ]; then
        # Uninstall KM components if we are doing a KM install.
        if [ -f install_km.sh -a -f $KMLOG ]; then
            echo "Uninstalling Kernel components"
            do_uninstall $KMLOG
            echo 
        fi
        # Uninstall UM components if we are doing a UM install.
        if [ -f install_um.sh -a -f $UMLOG ]; then
            echo "Uninstalling User components"
            do_uninstall $UMLOG
            echo 
        fi
    fi
}

# Work out if there are any special instructions.
#
while [ "$1" ]; do
    case "$1" in
    -v|--verbose)
        VERBOSE=v
        ;;
    -r|--root)
        DISCIMAGE=$2
        shift;
        ;;
    -u|--uninstall)
        UNINSTALL_ONLY=y
        ;;
    -n)
        DOIT=echo
        ;;
    -h | --help | *)
        usage
        ;;
    esac
    shift
done

if [ ! -z "$DISCIMAGE" ]; then

    if [ ! -d "$DISCIMAGE" ]; then
       bail "$0: $DISCIMAGE does not exist."
    fi

    echo
    if [ $DISCIMAGE == "/" ]; then
        echo "Installing PowerVR '$PVRVERSION ($PVRBUILD)' locally"
    else
        echo "Installing PowerVR '$PVRVERSION ($PVRBUILD)' on $DISCIMAGE"
    fi
    echo
    echo "File system installation root is $DISCIMAGE"
    echo

    OLDLOG=$DISCIMAGE/powervr_ddk_install.log
    KMLOG=$DISCIMAGE/powervr_ddk_install_km.log
    UMLOG=$DISCIMAGE/powervr_ddk_install_um.log

    uninstall_locally

    if [ "$UNINSTALL_ONLY" != "y" ]; then
        install_locally
    fi

else
    bail "DISCIMAGE must be set for installation to be possible."
fi
