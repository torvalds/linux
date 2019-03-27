# $NetBSD: quotas_common.sh,v 1.2 2011/03/06 17:08:41 bouyer Exp $ 

create_with_quotas()
{
	local endian=$1; shift
	local vers=$1; shift
	local uid=$(id -u)
	local gid=$(id -g)

	atf_check -o ignore -e ignore newfs -B ${endian} -O ${vers} \
		-s 4000 -F ${IMG}
	atf_check -o ignore -e ignore tunefs -q user -q group -F ${IMG}
	atf_check -s exit:0 -o 'match:NO USER QUOTA INODE \(CREATED\)' \
	    -o 'match:USER QUOTA MISMATCH FOR ID '${uid}': 0/0 SHOULD BE 1/1' \
	    -o 'match:GROUP QUOTA MISMATCH FOR ID '${gid}': 0/0 SHOULD BE 1/1' \
	    fsck_ffs -p -F ${IMG}
}

# from tests/ipf/h_common.sh via tests/sbin/resize_ffs
test_case()
{
	local name="${1}"; shift
	local check_function="${1}"; shift
	local descr="${1}"; shift
	
	atf_test_case "${name}"

	eval "${name}_head() { \
		atf_set "descr" "Checks ${descr} quotas inodes"
	}"
	eval "${name}_body() { \
		${check_function} " "${@}" "; \
	}"
	tests="${tests} ${name}"
}

atf_init_test_cases()
{
	IMG=fsimage
	DIR=target
	for i in ${tests}; do
		atf_add_test_case $i
	done
}
