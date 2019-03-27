#!/bin/bash
#
# Add the modified BSD license to a file
#

f=`mktemp -d`
trap "rm -rf $f" EXIT

year=`date +%Y`
cat > $f/original <<EOF
Copyright (c) $year, Linaro Limited
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Linaro nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
EOF

# Translate it to C style
echo "/*" > $f/c
sed -r 's/(.*)/ * \1/' $f/original | sed -r 's/ +$//' >> $f/c
echo " */" >> $f/c
echo >> $f/c

# ...and shell style
sed -r 's/(.*)/# \1/' $f/original | sed -r 's/ +$//' >> $f/shell
echo '#' >> $f/shell
echo >> $f/shell

for name in $@; do
    if grep -q Copyright $name; then
	echo $name already has some type of copyright
	continue
    fi

    case $name in
	# These files don't have an explicit license
        *autogen.sh*)
	    continue;;
	*reference/newlib/*)
	    continue;;
	*reference/newlib-xscale/*)
	    continue;;
	*/dhry/*)
	    continue;;

	*.c)
	    src=$f/c
	    ;;
	*.sh|*.am|*.ac)
	    src=$f/shell
	    ;;
	*)
	    echo Unrecognied extension on $name
	    continue
    esac

    cat $src $name > $f/next
    mv $f/next $name
    echo Updated $name
done
