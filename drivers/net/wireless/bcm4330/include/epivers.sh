#! /bin/bash
#
# Create the epivers.h file from epivers.h.in
# 
# Epivers.h generation mechanism supports svn based checkouts
#
# $Id: epivers.sh 241914 2011-02-20 03:11:27Z $
# 
# GetCompVer.py return value and action needed
#    i. trunk => use current date as version string
#   ii. local => use SVNURL expanded by HeadURL keyword
#  iii. <tag> => use it as as is
#                (some components can override and say give me native ver)
#   iv. empty =>
#             a) If TAG is specified use it
#             a) If no TAG is specified use date
#

# If the version header file already exists, increment its build number.
# Otherwise, create a new file.
if [ -f epivers.h ]; then
	build=`grep EPI_BUILD_NUMBER epivers.h | sed -e "s,.*BUILD_NUMBER[ 	]*,,"`
	build=`expr ${build} + 1`
	echo build=${build}
	sed -e "s,.*_BUILD_NUMBER.*,#define EPI_BUILD_NUMBER	${build}," \
		< epivers.h > epivers.h.new
	mv epivers.h epivers.h.prev
	mv epivers.h.new epivers.h
	exit 0

else # epivers.h doesn't exist

	NULL="/dev/null"
	svncmd="svn --non-interactive"

	# Check for the in file, if not there we're in the wrong directory
	if [ ! -f epivers.h.in ]; then
		echo No epivers.h.in found
		exit 1
	fi

	# Following SVNURL should be expanded on checkout
	SVNURL='$HeadURL: http://svn.sj.broadcom.com/svn/wlansvn/proj/tags/FALCON/FALCON_REL_5_90_125_69/src/include/epivers.sh $'

	# If SVNURL isn't expanded, extract it from svn info
	if echo "$SVNURL" | grep -vq "HeadURL.*/proj/.*"; then
		[ -n "$VERBOSE" ] && \
		echo "DBG: SVN URL wasn't expanded. Getting it from svn info"
		SVNURL=$($svncmd info epivers.sh 2> $NULL | egrep "^URL:")
	fi
	
	if echo "${TAG}" | grep -q "BRANCH\|TWIG"; then
		branchtag=$TAG
	else
		branchtag=""
	fi
	
	# If this is a tagged build, use the tag to supply the numbers
	# Tag should be in the form
	#    <NAME>_REL_<MAJ>_<MINOR>
	# or
	#    <NAME>_REL_<MAJ>_<MINOR>_RC<RCNUM>
	# or
	#    <NAME>_REL_<MAJ>_<MINOR>_RC<RCNUM>_<INCREMENTAL>

	SRCBASE=..
	MERGERLOG=${SRCBASE}/../merger_sources.log
	GETCOMPVER=getcompver.py
	GETCOMPVER_NET=/projects/hnd_software/gallery/src/tools/build/$GETCOMPVER
	GETCOMPVER_NET_WIN=Z:${GETCOMPVER_NET}

	#
	# If there is a local copy GETCOMPVER use it ahead of network copy
	#
	if [ -s "$GETCOMPVER" ]; then
	        GETCOMPVER_PATH="$GETCOMPVER"
	elif [ -s "${SRCBASE}/../src/tools/build/$GETCOMPVER" ]; then
	        GETCOMPVER_PATH="${SRCBASE}/../src/tools/build/$GETCOMPVER"
	elif [ -s "$GETCOMPVER_NET" ]; then
	        GETCOMPVER_PATH="$GETCOMPVER_NET"
	elif [ -s "$GETCOMPVER_NET_WIN" ]; then
	        GETCOMPVER_PATH="$GETCOMPVER_NET_WIN"
	fi

	#
	# If $GETCOMPVER isn't found, fetch it from SVN
	# (this should be very rare)
	#
	if [ ! -s "$GETCOMPVER_PATH" ]; then
		[ -n "$VERBOSE" ] && \
			echo "DBG: Fetching $GETCOMPVER from trunk"

		$svncmd export -q \
			^/proj/trunk/src/tools/build/${GETCOMPVER} \
			${GETCOMPVER} 2> $NULL

		GETCOMPVER_PATH=$GETCOMPVER
	fi

	# Now get tag for src/include from automerger log
	[ -n "$VERBOSE" ] && \
		echo "DBG: python $GETCOMPVER_PATH $MERGERLOG src/include"

	COMPTAG=$(python $GETCOMPVER_PATH $MERGERLOG src/include 2> $NULL)

	echo "DBG: Component Tag String Derived = $COMPTAG"

	# Process COMPTAG values
	# Rule:
	# If trunk is returned, use date as component tag
	# If LOCAL_COMPONENT is returned, use SVN URL to get native tag
	# If component is returned or empty, assign it to SVNTAG
	# GetCompVer.py return value and action needed
	#    i. trunk => use current date as version string
	#   ii. local => use SVNURL expanded by HeadURL keyword
	#  iii. <tag> => use it as as is
	#   iv. empty =>
	#             a) If TAG is specified use it
	#             a) If no TAG is specified use SVNURL from HeadURL

	SVNURL_VER=false

	if [ "$COMPTAG" == "" ]; then
		SVNURL_VER=true
	elif [ "$COMPTAG" == "LOCAL_COMPONENT" ]; then
		SVNURL_VER=true
	elif [ "$COMPTAG" == "trunk" ]; then
		SVNTAG=$(date '+TRUNKCOMP_REL_%Y_%m_%d')
	else
		SVNTAG=$COMPTAG
	fi

	# TODO Some of the echo statements will be removed, once
	# TODO SVN transition matures
	if [ "$SVNURL_VER" == "true" ]; then
		case "${SVNURL}" in
			*/branches/*) 	
				SVNTAG=$(echo $SVNURL | sed -e 's%.*/branches/\(.*\)/src.*%\1%g' | xargs printf "%s")
				;;
			*/tags/*) 	
				SVNTAG=$(echo $SVNURL | sed -e 's%.*/tags/\(.*\)/src.*%\1%g' | xargs printf "%s")
				;;
			*/trunk/*) 	
				SVNTAG=$(date '+TRUNKURL_REL_%Y_%m_%d')
				;;
			*)       	
				SVNTAG=$(date '+OTHER_REL_%Y_%m_%d')
				;;
		esac
		echo "DBG: Native Tag String Derived from URL: $SVNTAG"
	else
		echo "DBG: Native Tag String Derived: $SVNTAG"
	fi

	TAG=${SVNTAG}

	# Split the tag into an array on underbar or whitespace boundaries.
	IFS="_	     " tag=(${TAG})
	unset IFS

        tagged=1
	if [ ${#tag[*]} -eq 0 ]; then
	   tag=(`date '+TOT REL %Y %m %d 0 %y'`);
	   # reconstruct a TAG from the date
	   TAG=${tag[0]}_${tag[1]}_${tag[2]}_${tag[3]}_${tag[4]}_${tag[5]}	   
	   tagged=0
	fi

	# Allow environment variable to override values.
	# Missing values default to 0
	#
	maj=${EPI_MAJOR_VERSION:-${tag[2]:-0}}
	min=${EPI_MINOR_VERSION:-${tag[3]:-0}}
	rcnum=${EPI_RC_NUMBER:-${tag[4]:-0}}

	# If increment field is 0, set it to date suffix if on TOB
	if [ -n "$branchtag" ]; then
		[ "${tag[5]:-0}" -eq 0 ] && echo "Using date suffix for incr"
		today=`date '+%Y%m%d'`
		incremental=${EPI_INCREMENTAL_NUMBER:-${tag[5]:-${today:-0}}}
	else
		incremental=${EPI_INCREMENTAL_NUMBER:-${tag[5]:-0}}
	fi
	origincr=${EPI_INCREMENTAL_NUMBER:-${tag[5]:-0}}
	build=${EPI_BUILD_NUMBER:-0}

	# Strip 'RC' from front of rcnum if present
	rcnum=${rcnum/#RC/}
	
	# strip leading zero off the number (otherwise they look like octal)
	maj=${maj/#0/}
	min=${min/#0/}
	rcnum=${rcnum/#0/}
	incremental=${incremental/#0/}
	origincr=${origincr/#0/}
	build=${build/#0/}

	# some numbers may now be null.  replace with with zero.
	maj=${maj:-0}
	min=${min:-0}

	rcnum=${rcnum:-0}
	incremental=${incremental:-0}
	origincr=${origincr:-0}
	build=${build:-0}

	if [ ${tagged} -eq 1 ]; then
	    # vernum is 32chars max
	    vernum=`printf "0x%02x%02x%02x%02x" ${maj} ${min} ${rcnum} ${origincr}`
	else 
	    vernum=`printf "0x00%02x%02x%02x" ${tag[7]} ${min} ${rcnum}`
	fi

	# make sure the size of vernum is under 32 bits. 
	# Otherwise, truncate. The string will keep full information.
	vernum=${vernum:0:10}

	# build the string directly from the tag, irrespective of its length
	# remove the name , the tag type, then replace all _ by . 
	tag_ver_str=${TAG/${tag[0]}_}
	tag_ver_str=${tag_ver_str/${tag[1]}_}
	tag_ver_str=${tag_ver_str//_/.}

	# record tag type
	tagtype=

	if [ "${tag[1]}" = "BRANCH" -o "${tag[1]}" = "TWIG" ]; then
	   tagtype=" (TOB)"
	   echo "tag type: $tagtype"
	fi

	echo "Effective version string: $tag_ver_str"

	if [ "$(uname -s)" == "Darwin" ]; then
	   # Mac does not like 2-digit numbers so convert the number to single
	   # digit. 5.100 becomes 5.1
	   if [ $min -gt 99 ]; then
	       minmac=`expr $min / 100`
	   else
	       minmac=$min
	   fi
	   epi_ver_dev="${maj}.${minmac}.0"
	else
	   epi_ver_dev="${maj}.${min}.${rcnum}"
	fi

	# OK, go do it
	echo "maj=${maj}, min=${min}, rc=${rcnum}, inc=${incremental}, build=${build}"

	sed \
		-e "s;@EPI_MAJOR_VERSION@;${maj};" \
		-e "s;@EPI_MINOR_VERSION@;${min};" \
		-e "s;@EPI_RC_NUMBER@;${rcnum};" \
		-e "s;@EPI_INCREMENTAL_NUMBER@;${incremental};" \
		-e "s;@EPI_BUILD_NUMBER@;${build};" \
		-e "s;@EPI_VERSION@;${maj}, ${min}, ${rcnum}, ${incremental};" \
		-e "s;@EPI_VERSION_STR@;${tag_ver_str};" \
		-e "s;@EPI_VERSION_TYPE@;${tagtype};" \
		-e "s;@VERSION_TYPE@;${tagtype};" \
                -e "s;@EPI_VERSION_NUM@;${vernum};" \
		-e "s;@EPI_VERSION_DEV@;${epi_ver_dev};" \
		< epivers.h.in > epivers.h

fi # epivers.h
