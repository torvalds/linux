# Copyright (c) 2007 The NetBSD Foundation, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
# CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

create_files()
{
    mkdir tmp
    touch tmp/datafile
}

atf_test_case default
default_head()
{
    atf_set "descr" "Checks that the program can find its files if" \
                    "executed from the same directory"
}
default_body()
{
    create_files

    for hp in $(get_helpers); do
        h=${hp##*/}
        cp ${hp} tmp
        atf_check -s eq:0 -o ignore -e ignore -x \
                  "cd tmp && ./${h} srcdir_exists"
        atf_check -s eq:1 -o empty -e ignore "${hp}" -r res srcdir_exists
        atf_check -s eq:0 -o ignore -e empty grep "Cannot find datafile" res
    done
}

atf_test_case libtool
libtool_head()
{
    atf_set "descr" "Checks that the program can find its files if" \
                    "executed from the source directory and if it" \
                    "was built with libtool"
}
libtool_body()
{
    create_files
    mkdir tmp/.libs

    for hp in $(get_helpers c_helpers cpp_helpers); do
        h=${hp##*/}
        cp ${hp} tmp
        cp ${hp} tmp/.libs
        atf_check -s eq:0 -o ignore -e ignore -x \
                  "cd tmp && ./.libs/${h} srcdir_exists"
        atf_check -s eq:1 -o empty -e ignore "${hp}" -r res srcdir_exists
        atf_check -s eq:0 -o ignore -e empty grep "Cannot find datafile" res
    done

    for hp in $(get_helpers c_helpers cpp_helpers); do
        h=${hp##*/}
        cp ${hp} tmp
        cp ${hp} tmp/.libs/lt-${h}
        atf_check -s eq:0 -o ignore -e ignore -x \
                  "cd tmp && ./.libs/lt-${h} srcdir_exists"
        atf_check -s eq:1 -o empty -e ignore "${hp}" -r res srcdir_exists
        atf_check -s eq:0 -o ignore -e empty grep "Cannot find datafile" res
    done
}

atf_test_case sflag
sflag_head()
{
    atf_set "descr" "Checks that the program can find its files when" \
                    "using the -s flag"
}
sflag_body()
{
    create_files

    for hp in $(get_helpers); do
        h=${hp##*/}
        cp ${hp} tmp
        atf_check -s eq:0 -o ignore -e ignore -x \
                  "cd tmp && ./${h} -s $(pwd)/tmp \
                   srcdir_exists"
        atf_check -s eq:1 -o empty -e save:stderr "${hp}" -r res srcdir_exists
        atf_check -s eq:0 -o ignore -e empty grep "Cannot find datafile" res
        atf_check -s eq:0 -o ignore -e ignore \
                  "${hp}" -s "$(pwd)"/tmp srcdir_exists
    done
}

atf_test_case relative
relative_head()
{
    atf_set "descr" "Checks that passing a relative path through -s" \
                    "works"
}
relative_body()
{
    create_files

    for hp in $(get_helpers); do
        h=${hp##*/}
        cp ${hp} tmp

        for p in tmp tmp/. ./tmp; do
            echo "Helper is: ${h}"
            echo "Using source directory: ${p}"

            atf_check -s eq:0 -o ignore -e ignore \
                      "./tmp/${h}" -s "${p}" srcdir_exists
            atf_check -s eq:1 -o empty -e save:stderr "${hp}" -r res \
                srcdir_exists
            atf_check -s eq:0 -o ignore -e empty grep "Cannot find datafile" res
            atf_check -s eq:0 -o ignore -e ignore \
                      "${hp}" -s "${p}" srcdir_exists
        done
    done
}

atf_init_test_cases()
{
    atf_add_test_case default
    atf_add_test_case libtool
    atf_add_test_case sflag
    atf_add_test_case relative
}

# vim: syntax=sh:expandtab:shiftwidth=4:softtabstop=4
