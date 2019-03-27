# $NetBSD: t_mixerctl.sh,v 1.1 2017/01/02 15:40:09 christos Exp $

atf_test_case noargs_usage
noargs_usage_head() {
	atf_set "descr" "Ensure mixerctl(1) with no args prints a usage message"
}
noargs_usage_body() {
	atf_check -s exit:0 -o not-empty -e ignore \
		mixerctl
}

atf_test_case showvalue
showvalue_head() {
	atf_set "descr" "Ensure mixerctl(1) can print the value for all variables"
}
showvalue_body() {
	for var in $(mixerctl -a | awk -F= '{print $1}'); do
		atf_check -s exit:0 -e ignore -o match:"^${var}=" \
			mixerctl ${var}
	done
}

atf_test_case nflag
nflag_head() {
	atf_set "descr" "Ensure 'mixerctl -n' actually suppresses some output"
}
nflag_body() {
	varname="$(mixerctl -a | head -1 | awk -F= '{print $1}')"

	atf_check -s exit:0 -o match:"${varname}" -e ignore \
		mixerctl ${varname}

	atf_check -s exit:0 -o not-match:"${varname}" -e ignore \
		mixerctl -n ${varname}
}

atf_test_case nonexistant_device
nonexistant_device_head() {
	atf_set "descr" "Ensure mixerctl(1) complains if provided a nonexistant mixer device"
}
nonexistant_device_body() {
	atf_check -s not-exit:0  -o ignore -e match:"No such file" \
		mixerctl -d /a/b/c/d/e
}

atf_init_test_cases() {
	atf_add_test_case noargs_usage
	atf_add_test_case showvalue
	atf_add_test_case nflag
	atf_add_test_case nonexistant_device
}
