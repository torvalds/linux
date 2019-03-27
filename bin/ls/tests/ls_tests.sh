#
# Copyright 2015 EMC Corp.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#

create_test_dir()
{
	[ -z "$ATF_TMPDIR" ] || return 0

	export ATF_TMPDIR=$(pwd)

	# XXX: need to nest this because of how kyua creates $TMPDIR; otherwise
	# it will run into EPERM issues later
	TEST_INPUTS_DIR="${ATF_TMPDIR}/test/inputs"

	atf_check -e empty -s exit:0 mkdir -m 0777 -p $TEST_INPUTS_DIR
	cd $TEST_INPUTS_DIR
}

create_test_inputs()
{
	create_test_dir

	atf_check -e empty -s exit:0 mkdir -m 0755 -p a/b/1
	atf_check -e empty -s exit:0 ln -s a/b c
	atf_check -e empty -s exit:0 touch d
	atf_check -e empty -s exit:0 ln d e
	atf_check -e empty -s exit:0 touch .f
	atf_check -e empty -s exit:0 mkdir .g
	atf_check -e empty -s exit:0 mkfifo h
	atf_check -e ignore -s exit:0 dd if=/dev/zero of=i count=1000 bs=1
	atf_check -e empty -s exit:0 touch klmn
	atf_check -e empty -s exit:0 touch opqr
	atf_check -e empty -s exit:0 touch stuv
	atf_check -e empty -s exit:0 install -m 0755 /dev/null wxyz
	atf_check -e empty -s exit:0 touch 0b00000001
	atf_check -e empty -s exit:0 touch 0b00000010
	atf_check -e empty -s exit:0 touch 0b00000011
	atf_check -e empty -s exit:0 touch 0b00000100
	atf_check -e empty -s exit:0 touch 0b00000101
	atf_check -e empty -s exit:0 touch 0b00000110
	atf_check -e empty -s exit:0 touch 0b00000111
	atf_check -e empty -s exit:0 touch 0b00001000
	atf_check -e empty -s exit:0 touch 0b00001001
	atf_check -e empty -s exit:0 touch 0b00001010
	atf_check -e empty -s exit:0 touch 0b00001011
	atf_check -e empty -s exit:0 touch 0b00001100
	atf_check -e empty -s exit:0 touch 0b00001101
	atf_check -e empty -s exit:0 touch 0b00001110
	atf_check -e empty -s exit:0 touch 0b00001111
}

KB=1024
MB=$(( 1024 * $KB ))
GB=$(( 1024 * $MB ))
TB=$(( 1024 * $GB ))
PB=$(( 1024 * $TB ))

create_test_inputs2()
{
	create_test_dir

	if ! getconf MIN_HOLE_SIZE "$(pwd)"; then
	    echo "getconf MIN_HOLE_SIZE $(pwd) failed; sparse files probably" \
	         "not supported by file system"
	    mount
	    atf_skip "Test's work directory does not support sparse files;" \
	             "try with a different TMPDIR?"
	fi

	for filesize in 1 512 $(( 2 * $KB )) $(( 10 * $KB )) $(( 512 * $KB )); \
	do
		atf_check -e ignore -o empty -s exit:0 \
		    dd if=/dev/zero of=${filesize}.file bs=1 \
		    count=1 oseek=${filesize} conv=sparse
		files="${files} ${filesize}.file"
	done

	for filesize in $MB $GB $TB; do
		atf_check -e ignore -o empty -s exit:0 \
		    dd if=/dev/zero of=${filesize}.file bs=$MB \
		    count=1 oseek=$(( $filesize / $MB )) conv=sparse
		files="${files} ${filesize}.file"
	done
}

atf_test_case A_flag
A_flag_head()
{
	atf_set "descr" "Verify -A support with unprivileged users"
}

A_flag_body()
{
	create_test_dir

	atf_check -e empty -o empty -s exit:0 ls -A

	create_test_inputs

	WITH_A=$PWD/../with_A.out
	WITHOUT_A=$PWD/../without_A.out

	atf_check -e empty -o save:$WITH_A -s exit:0 ls -A
	atf_check -e empty -o save:$WITHOUT_A -s exit:0 ls

	echo "-A usage"
	cat $WITH_A
	echo "No -A usage"
	cat $WITHOUT_A

	for dot_path in '\.f' '\.g'; do
		atf_check -e empty -o not-empty -s exit:0 grep "${dot_path}" \
		    $WITH_A
		atf_check -e empty -o empty -s not-exit:0 grep "${dot_path}" \
		    $WITHOUT_A
	done
}

atf_test_case A_flag_implied_when_root
A_flag_implied_when_root_head()
{
	atf_set "descr" "Verify that -A is implied for root"
	atf_set "require.user" "root"
}

A_flag_implied_when_root_body()
{
	create_test_dir

	atf_check -e empty -o empty -s exit:0 ls -A

	create_test_inputs

	WITH_EXPLICIT=$PWD/../with_explicit_A.out
	WITH_IMPLIED=$PWD/../with_implied_A.out

	atf_check -e empty -o save:$WITH_EXPLICIT -s exit:0 ls -A
	atf_check -e empty -o save:$WITH_IMPLIED -s exit:0 ls

	echo "Explicit -A usage"
	cat $WITH_EXPLICIT
	echo "Implicit -A usage"
	cat $WITH_IMPLIED

	atf_check_equal "$(cat $WITH_EXPLICIT)" "$(cat $WITH_IMPLIED)"
}

atf_test_case B_flag
B_flag_head()
{
	atf_set "descr" "Verify that the output from ls -B prints out non-printable characters"
}

B_flag_body()
{
	atf_check -e empty -o empty -s exit:0 touch "$(printf "y\013z")"
	atf_check -e empty -o match:'y\\013z' -s exit:0 ls -B
}

atf_test_case C_flag
C_flag_head()
{
	atf_set "descr" "Verify that the output from ls -C is multi-column, sorted down"
}

print_index()
{
	local i=1
	local wanted_index=$1; shift

	while [ $i -le $wanted_index ]; do
		if [ $i -eq $wanted_index ]; then
			echo $1
			return
		fi
		shift
		: $(( i += 1 ))
	done
}

C_flag_body()
{
	create_test_inputs

	WITH_C=$PWD/../with_C.out

	export COLUMNS=40
	atf_check -e empty -o save:$WITH_C -s exit:0 ls -C

	echo "With -C usage"
	cat $WITH_C

	paths=$(find -s . -mindepth 1 -maxdepth 1 \! -name '.*' -exec basename {} \; )
	set -- $paths
	num_paths=$#
	num_columns=2

	max_num_paths_per_column=$(( $(( $num_paths + 1 )) / $num_columns ))

	local i=1
	while [ $i -le $max_num_paths_per_column ]; do
		column_1=$(print_index $i $paths)
		column_2=$(print_index $(( $i + $max_num_paths_per_column )) $paths)
		#echo "paths[$(( $i + $max_num_paths_per_column ))] = $column_2"
		expected_expr="$column_1"
		if [ -n "$column_2" ]; then
			expected_expr="$expected_expr[[:space:]]+$column_2"
		fi
		atf_check -e ignore -o not-empty -s exit:0 \
		    egrep "$expected_expr" $WITH_C
		: $(( i += 1 ))
	done
}

atf_test_case D_flag
D_flag_head()
{
	atf_set "descr" "Verify that the output from ls -D modifies the time format used with ls -l"
}

D_flag_body()
{
	atf_check -e empty -o empty -s exit:0 touch a.file
	atf_check -e empty -o match:"$(stat -f '%c[[:space:]]+%N' a.file)" \
	    -s exit:0 ls -lD '%s'
}

atf_test_case F_flag
F_flag_head()
{
	atf_set "descr" "Verify that the output from ls -F prints out appropriate symbols after files"
}

F_flag_body()
{
	create_test_inputs

	atf_check -e empty -s exit:0 \
	    sh -c "pid=${ATF_TMPDIR}/nc.pid; daemon -p \$pid nc -lU j; sleep 2; pkill -F \$pid"

	atf_check -e empty -o match:'a/' -s exit:0 ls -F
	atf_check -e empty -o match:'c@' -s exit:0 ls -F
	atf_check -e empty -o match:'h\|' -s exit:0 ls -F
	atf_check -e empty -o match:'j=' -s exit:0 ls -F
	#atf_check -e empty -o match:'<whiteout-file>%' -s exit:0 ls -F
	atf_check -e empty -o match:'stuv' -s exit:0 ls -F
	atf_check -e empty -o match:'wxyz\*' -s exit:0 ls -F
}

atf_test_case H_flag
H_flag_head()
{
	atf_set "descr" "Verify that ls -H follows symlinks"
}

H_flag_body()
{
	create_test_inputs

	atf_check -e empty -o match:'1' -s exit:0 ls -H c
}

atf_test_case I_flag
I_flag_head()
{
	atf_set "descr" "Verify that the output from ls -I is the same as ls for an unprivileged user"
}

I_flag_body()
{
	create_test_inputs

	WITH_I=$PWD/../with_I.out
	WITHOUT_I=$PWD/../without_I.out

	atf_check -e empty -o save:$WITH_I -s exit:0 ls -I
	atf_check -e empty -o save:$WITHOUT_I -s exit:0 ls

	echo "Explicit -I usage"
	cat $WITH_I
	echo "No -I usage"
	cat $WITHOUT_I

	atf_check_equal "$(cat $WITH_I)" "$(cat $WITHOUT_I)"
}

atf_test_case I_flag_voids_implied_A_flag_when_root
I_flag_voids_implied_A_flag_when_root_head()
{
	atf_set "descr" "Verify that -I voids out implied -A for root"
	atf_set "require.user" "root"
}

I_flag_voids_implied_A_flag_when_root_body()
{
	create_test_inputs

	atf_check -o not-match:'\.f' -s exit:0 ls -I
	atf_check -o not-match:'\.g' -s exit:0 ls -I

	atf_check -o match:'\.f' -s exit:0 ls -A -I
	atf_check -o match:'\.g' -s exit:0 ls -A -I
}

atf_test_case L_flag
L_flag_head()
{
	atf_set "descr" "Verify that -L prints out the symbolic link and conversely -P prints out the target for the symbolic link"
}

L_flag_body()
{
	atf_check -e empty -o empty -s exit:0 ln -s target1/target2 link1
	atf_check -e empty -o match:link1 -s exit:0 ls -L
	atf_check -e empty -o not-match:target1/target2 -s exit:0 ls -L
}

atf_test_case R_flag
R_flag_head()
{
	atf_set "descr" "Verify that the output from ls -R prints out the directory contents recursively"
}

R_flag_body()
{
	create_test_inputs

	WITH_R=$PWD/../with_R.out
	WITH_R_expected_output=$PWD/../with_R_expected.out

	atf_check -e empty -o save:$WITH_R -s exit:0 ls -R

	set -- . $(find -s . \! -name '.*' -type d)
	while [ $# -gt 0 ]; do
		dir=$1; shift
		[ "$dir" != "." ] && echo "$dir:"
		(cd $dir && ls -1A | sed -e '/^\./d')
		[ $# -ne 0 ] && echo
	done > $WITH_R_expected_output

	echo "-R usage"
	cat $WITH_R
	echo "-R expected output"
	cat $WITH_R_expected_output

	atf_check_equal "$(cat $WITH_R)" "$(cat $WITH_R_expected_output)"
}

atf_test_case S_flag
S_flag_head()
{
	atf_set "descr" "Verify that -S sorts by file size, then by filename lexicographically"
}

S_flag_body()
{
	create_test_dir

	file_list_dir=$PWD/../files

	atf_check -e empty -o empty -s exit:0 mkdir -p $file_list_dir

	create_test_inputs
	create_test_inputs2

	WITH_S=$PWD/../with_S.out
	WITHOUT_S=$PWD/../without_S.out

	atf_check -e empty -o save:$WITH_S ls -D '%s' -lS
	atf_check -e empty -o save:$WITHOUT_S ls -D '%s' -l

	WITH_S_parsed=$(awk '! /^total/ { print $7 }' $WITH_S)
	set -- $(awk '! /^total/ { print $5, $7 }' $WITHOUT_S)
	while [ $# -gt 0 ]; do
		size=$1; shift
		filename=$1; shift
		echo $filename >> $file_list_dir/${size}
	done
	file_lists=$(find $file_list_dir -type f -exec basename {} \; | sort -nr)
	WITHOUT_S_parsed=$(for file_list in $file_lists; do sort < $file_list_dir/$file_list; done)

	echo "-lS usage (parsed)"
	echo "$WITH_S_parsed"
	echo "-l usage (parsed)"
	echo "$WITHOUT_S_parsed"

	atf_check_equal "$WITHOUT_S_parsed" "$WITH_S_parsed"
}

atf_test_case T_flag
T_flag_head()
{
	atf_set "descr" "Verify -T support"
}

T_flag_body()
{
	create_test_dir

	atf_check -e empty -o empty -s exit:0 touch a.file

	mtime_in_secs=$(stat -f %m -t %s a.file)
	mtime=$(date -j -f %s $mtime_in_secs +"[[:space:]]+%b[[:space:]]+%e[[:space:]]+%H:%M:%S[[:space:]]+%Y")

	atf_check -e empty -o match:"$mtime"'[[:space:]]+a\.file' \
	    -s exit:0 ls -lT a.file
}

atf_test_case a_flag
a_flag_head()
{
	atf_set "descr" "Verify -a support"
}

a_flag_body()
{
	create_test_dir

	# Make sure "." and ".." show up with -a
	atf_check -e empty -o match:'\.[[:space:]]+\.\.'  -s exit:0 ls -ax

	create_test_inputs

	WITH_a=$PWD/../with_a.out
	WITHOUT_a=$PWD/../without_a.out

	atf_check -e empty -o save:$WITH_a -s exit:0 ls -a
	atf_check -e empty -o save:$WITHOUT_a -s exit:0 ls

	echo "-a usage"
	cat $WITH_a
	echo "No -a usage"
	cat $WITHOUT_a

	for dot_path in '\.f' '\.g'; do
		atf_check -e empty -o not-empty -s exit:0 grep "${dot_path}" \
		    $WITH_a
		atf_check -e empty -o empty -s not-exit:0 grep "${dot_path}" \
		    $WITHOUT_a
	done
}

atf_test_case b_flag
b_flag_head()
{
	atf_set "descr" "Verify that the output from ls -b prints out non-printable characters"
}

b_flag_body()
{
	atf_check -e empty -o empty -s exit:0 touch "$(printf "y\013z")"
	atf_check -e empty -o match:'y\\vz' -s exit:0 ls -b
}

atf_test_case d_flag
d_flag_head()
{
	atf_set "descr" "Verify that -d doesn't descend down directories"
}

d_flag_body()
{
	create_test_dir

	output=$PWD/../output

	atf_check -e empty -o empty -s exit:0 mkdir -p a/b

	for path in . $PWD a; do
		atf_check -e empty -o save:$output -s exit:0 ls -d $path
		atf_check_equal "$(cat $output)" "$path"
	done
}

atf_test_case f_flag
f_flag_head()
{
	atf_set "descr" "Verify that -f prints out the contents of a directory unsorted"
}

f_flag_body()
{
	create_test_inputs

	output=$PWD/../output

	# XXX: I don't have enough understanding of how the algorithm works yet
	# to determine more than the fact that all the entries printed out
	# exist
	paths=$(find -s . -mindepth 1 -maxdepth 1 \! -name '.*' -exec basename {} \; )

	atf_check -e empty -o save:$output -s exit:0 ls -f

	for path in $paths; do
		atf_check -e ignore -o not-empty -s exit:0 \
		    egrep "^$path$" $output
	done
}

atf_test_case g_flag
g_flag_head()
{
	atf_set "descr" "Verify that -g does nothing (compatibility flag)"
}

g_flag_body()
{
	create_test_inputs2
	for file in $files; do
		atf_check -e empty -o match:"$(ls -a $file)" -s exit:0 \
		    ls -ag $file
		atf_check -e empty -o match:"$(ls -la $file)" -s exit:0 \
		    ls -alg $file
	done
}

atf_test_case h_flag
h_flag_head()
{
	atf_set "descr" "Verify that -h prints out the humanized units for file sizes with ls -l"
	atf_set "require.progs" "bc"
}

h_flag_body()
{
	# XXX: this test doesn't currently show how 999 bytes will be 999B,
	# but 1000 bytes will be 1.0K, due to how humanize_number(3) works.
	create_test_inputs2
	for file in $files; do
		file_size=$(stat -f '%z' "$file") || \
		    atf_fail "stat'ing $file failed"
		scale=2
		if [ $file_size -lt $KB ]; then
			divisor=1
			scale=0
			suffix=B
		elif [ $file_size -lt $MB ]; then
			divisor=$KB
			suffix=K
		elif [ $file_size -lt $GB ]; then
			divisor=$MB
			suffix=M
		elif [ $file_size -lt $TB ]; then
			divisor=$GB
			suffix=G
		elif [ $file_size -lt $PB ]; then
			divisor=$TB
			suffix=T
		else
			divisor=$PB
			suffix=P
		fi

		bc_expr="$(printf "scale=%s\n%s/%s\nquit" $scale $file_size $divisor)"
		size_humanized=$(bc -e "$bc_expr" | tr '.' '\.' | sed -e 's,\.00,,')

		atf_check -e empty -o match:"$size_humanized.+$file" \
		    -s exit:0 ls -hl $file
	done
}

atf_test_case i_flag
i_flag_head()
{
	atf_set "descr" "Verify that -i prints out the inode for files"
}

i_flag_body()
{
	create_test_inputs

	paths=$(find -L . -mindepth 1)
	[ -n "$paths" ] || atf_skip 'Could not find any paths to iterate over (!)'

	for path in $paths; do
		atf_check -e empty \
		    -o match:"$(stat -f '[[:space:]]*%i[[:space:]]+%N' $path)" \
		    -s exit:0 ls -d1i $path
	done
}

atf_test_case k_flag
k_flag_head()
{
	atf_set "descr" "Verify that -k prints out the size with a block size of 1kB"
}

k_flag_body()
{
	create_test_inputs2
	for file in $files; do
		atf_check -e empty \
		    -o match:"[[:space:]]+$(stat -f "%z" $file)[[:space:]]+.+[[:space:]]+$file" ls -lk $file
	done
}

atf_test_case l_flag
l_flag_head()
{
	atf_set "descr" "Verify that -l prints out the output in long format"
}

l_flag_body()
{

	atf_check -e empty -o empty -s exit:0 touch a.file

	mtime_in_secs=$(stat -f "%m" -t "%s" a.file)
	mtime=$(date -j -f "%s" $mtime_in_secs +"%b[[:space:]]+%e[[:space:]]+%H:%M")

	expected_output=$(stat -f "%Sp[[:space:]]+%l[[:space:]]+%Su[[:space:]]+%Sg[[:space:]]+%z[[:space:]]+$mtime[[:space:]]+a\\.file" a.file)

	atf_check -e empty -o match:"$expected_output" -s exit:0 ls -l a.file
}

atf_test_case lcomma_flag
lcomma_flag_head()
{
	atf_set "descr" "Verify that -l, prints out the size with ',' delimiters"
}

lcomma_flag_body()
{
	create_test_inputs

	atf_check \
	    -o match:'\-rw\-r\-\-r\-\-[[:space:]]+.+[[:space:]]+1,000[[:space:]]+.+i' \
	    env LC_ALL=en_US.ISO8859-1 ls -l, i
}

atf_test_case m_flag
m_flag_head()
{
	atf_set "descr" "Verify that the output from ls -m is comma-separated"
}

m_flag_body()
{
	create_test_dir

	output=$PWD/../output

	atf_check -e empty -o empty -s exit:0 touch ,, "a,b " c d e

	atf_check -e empty -o save:$output -s exit:0 ls -m

	atf_check_equal "$(cat $output)" ",,, a,b , c, d, e"
}

atf_test_case n_flag
n_flag_head()
{
	atf_set "descr" "Verify that the output from ls -n prints out numeric GIDs/UIDs instead of symbolic GIDs/UIDs"
	atf_set "require.user" "root"
}

n_flag_body()
{
	daemon_gid=$(id -g daemon) || atf_skip "could not resolve gid for daemon (!)"
	nobody_uid=$(id -u nobody) || atf_skip "could not resolve uid for nobody (!)"

	atf_check -e empty -o empty -s exit:0 touch a.file
	atf_check -e empty -o empty -s exit:0 chown $nobody_uid:$daemon_gid a.file

	atf_check -e empty \
	    -o match:'\-rw\-r\-\-r\-\-[[:space:]]+1[[:space:]]+'"$nobody_uid[[:space:]]+$daemon_gid"'[[:space:]]+.+a\.file' \
	    ls -ln a.file

}

atf_test_case o_flag
o_flag_head()
{
	atf_set "descr" "Verify that the output from ls -o prints out the chflag values or '-' if none are set"
}

o_flag_body()
{
	local size=12345

	create_test_dir

	atf_check -e ignore -o empty -s exit:0 dd if=/dev/zero of=a.file \
	    bs=$size count=1
	atf_check -e ignore -o empty -s exit:0 dd if=/dev/zero of=b.file \
	    bs=$size count=1
	atf_check -e empty -o empty -s exit:0 chflags uarch a.file
	atf_check -e empty -o empty -s exit:0 chflags 0 b.file

	atf_check -e empty -o match:"[[:space:]]+uarch[[:space:]]$size+.+a\\.file" \
	    -s exit:0 ls -lo a.file
	atf_check -e empty -o match:"[[:space:]]+\\-[[:space:]]$size+.+b\\.file" \
	    -s exit:0 ls -lo b.file
}

atf_test_case p_flag
p_flag_head()
{
	atf_set "descr" "Verify that the output from ls -p prints out '/' after directories"
}

p_flag_body()
{
	create_test_inputs

	paths=$(find -L .)
	[ -n "$paths" ] || atf_skip 'Could not find any paths to iterate over (!)'

	for path in $paths; do
		suffix=
		# If path is not a symlink and is a directory, then the suffix
		# must be "/".
		if [ ! -L "${path}" -a -d "$path" ]; then
			suffix=/
		fi
		atf_check -e empty -o match:"$path${suffix}" -s exit:0 \
		    ls -dp $path
	done
}

atf_test_case q_flag_and_w_flag
q_flag_and_w_flag_head()
{
	atf_set "descr" "Verify that the output from ls -q prints out '?' for ESC and ls -w prints out the escape character"
}

q_flag_and_w_flag_body()
{
	create_test_dir

	test_file="$(printf "y\01z")"

	atf_check -e empty -o empty -s exit:0 touch "$test_file"

	atf_check -e empty -o match:'y\?z' -s exit:0 ls -q "$test_file"
	atf_check -e empty -o match:"$test_file" -s exit:0 ls -w "$test_file"
}

atf_test_case r_flag
r_flag_head()
{
	atf_set "descr" "Verify that the output from ls -r sorts the same way as reverse sorting with sort(1)"
}

r_flag_body()
{
	create_test_inputs

	WITH_r=$PWD/../with_r.out
	WITH_sort=$PWD/../with_sort.out

	atf_check -e empty -o save:$WITH_r -s exit:0 ls -1r
	atf_check -e empty -o save:$WITH_sort -s exit:0 sh -c 'ls -1 | sort -r'

	echo "Sorted with -r"
	cat $WITH_r
	echo "Reverse sorted with sort(1)"
	cat $WITH_sort

	atf_check_equal "$(cat $WITH_r)" "$(cat $WITH_sort)"
}

atf_test_case s_flag
s_flag_head()
{
	atf_set "descr" "Verify that the output from ls -s matches the output from stat(1)"
}

s_flag_body()
{
	create_test_inputs2
	for file in $files; do
		atf_check -e empty \
		    -o match:"$(stat -f "%b" $file)[[:space:]]+$file" ls -s $file
	done
}

atf_test_case t_flag
t_flag_head()
{
	atf_set "descr" "Verify that the output from ls -t sorts by modification time"
}

t_flag_body()
{
	create_test_dir

	atf_check -e empty -o empty -s exit:0 touch a.file
	atf_check -e empty -o empty -s exit:0 touch b.file

	atf_check -e empty -o match:'a\.file' -s exit:0 sh -c 'ls -lt | tail -n 1'
	atf_check -e empty -o match:'b\.file.*a\.file' -s exit:0 ls -Ct

	atf_check -e empty -o empty -s exit:0 rm a.file
	atf_check -e empty -o empty -s exit:0 sh -c 'echo "i am a" > a.file'

	atf_check -e empty -o match:'b\.file' -s exit:0 sh -c 'ls -lt | tail -n 1'
	atf_check -e empty -o match:'a\.file.*b\.file' -s exit:0 ls -Ct
}

atf_test_case u_flag
u_flag_head()
{
	atf_set "descr" "Verify that the output from ls -u sorts by last access"
}

u_flag_body()
{
	create_test_dir

	atf_check -e empty -o empty -s exit:0 touch a.file
	atf_check -e empty -o empty -s exit:0 touch b.file

	atf_check -e empty -o match:'b\.file' -s exit:0 sh -c 'ls -lu | tail -n 1'
	atf_check -e empty -o match:'a\.file.*b\.file' -s exit:0 ls -Cu

	atf_check -e empty -o empty -s exit:0 sh -c 'echo "i am a" > a.file'
	atf_check -e empty -o match:'i am a' -s exit:0 cat a.file

	atf_check -e empty -o match:'b\.file' -s exit:0 sh -c 'ls -lu | tail -n 1'
	atf_check -e empty -o match:'a\.file.*b\.file' -s exit:0 ls -Cu
}

atf_test_case x_flag
x_flag_head()
{
	atf_set "descr" "Verify that the output from ls -x is multi-column, sorted across"
}

x_flag_body()
{
	create_test_inputs

	WITH_x=$PWD/../with_x.out

	atf_check -e empty -o save:$WITH_x -s exit:0 ls -x

	echo "With -x usage"
	cat $WITH_x

	atf_check -e ignore -o not-empty -s exit:0 \
	    egrep "a[[:space:]]+c[[:space:]]+d[[:space:]]+e[[:space:]]+h" $WITH_x
	atf_check -e ignore -o not-empty -s exit:0 \
	    egrep "i[[:space:]]+klmn[[:space:]]+opqr[[:space:]]+stuv[[:space:]]+wxyz" $WITH_x
}

atf_test_case y_flag
y_flag_head()
{
	atf_set "descr" "Verify that the output from ls -y sorts the same way as sort(1)"
}

y_flag_body()
{
	create_test_inputs

	WITH_sort=$PWD/../with_sort.out
	WITH_y=$PWD/../with_y.out

	atf_check -e empty -o save:$WITH_sort -s exit:0 sh -c 'ls -1 | sort'
	atf_check -e empty -o save:$WITH_y -s exit:0 ls -1y

	echo "Sorted with sort(1)"
	cat $WITH_sort
	echo "Sorted with -y"
	cat $WITH_y

	atf_check_equal "$(cat $WITH_sort)" "$(cat $WITH_y)"
}

atf_test_case 1_flag
1_flag_head()
{
	atf_set "descr" "Verify that -1 prints out one item per line"
}

1_flag_body()
{
	create_test_inputs

	WITH_1=$PWD/../with_1.out
	WITHOUT_1=$PWD/../without_1.out

	atf_check -e empty -o save:$WITH_1 -s exit:0 ls -1
	atf_check -e empty -o save:$WITHOUT_1 -s exit:0 \
		sh -c 'for i in $(ls); do echo $i; done'

	echo "Explicit -1 usage"
	cat $WITH_1
	echo "No -1 usage"
	cat $WITHOUT_1

	atf_check_equal "$(cat $WITH_1)" "$(cat $WITHOUT_1)"
}

atf_init_test_cases()
{
	export BLOCKSIZE=512

	atf_add_test_case A_flag
	atf_add_test_case A_flag_implied_when_root
	atf_add_test_case B_flag
	atf_add_test_case C_flag
	atf_add_test_case D_flag
	atf_add_test_case F_flag
	#atf_add_test_case G_flag
	atf_add_test_case H_flag
	atf_add_test_case I_flag
	atf_add_test_case I_flag_voids_implied_A_flag_when_root
	atf_add_test_case L_flag
	#atf_add_test_case P_flag
	atf_add_test_case R_flag
	atf_add_test_case S_flag
	atf_add_test_case T_flag
	#atf_add_test_case U_flag
	#atf_add_test_case W_flag
	#atf_add_test_case Z_flag
	atf_add_test_case a_flag
	atf_add_test_case b_flag
	#atf_add_test_case c_flag
	atf_add_test_case d_flag
	atf_add_test_case f_flag
	atf_add_test_case g_flag
	atf_add_test_case h_flag
	atf_add_test_case i_flag
	atf_add_test_case k_flag
	atf_add_test_case l_flag
	atf_add_test_case lcomma_flag
	atf_add_test_case m_flag
	atf_add_test_case n_flag
	atf_add_test_case o_flag
	atf_add_test_case p_flag
	atf_add_test_case q_flag_and_w_flag
	atf_add_test_case r_flag
	atf_add_test_case s_flag
	atf_add_test_case t_flag
	atf_add_test_case u_flag
	atf_add_test_case x_flag
	atf_add_test_case y_flag
	atf_add_test_case 1_flag
}
