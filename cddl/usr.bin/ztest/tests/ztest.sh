#
# Test Case: ztest
# $FreeBSD$
#
atf_test_case ztest
ztest_head()
{
	atf_set "descr" "Run ztest"
	atf_set "timeout" 900
	atf_set "require.config" "rt_long"
}

ztest_body()
{
	ARGS="-VVVVV -f ${TMPDIR:-/tmp}"
	if atf_config_has ztest_extra_args; then
		ARGS="${ARGS} $(atf_config_get ztest_extra_args)"
	fi
	ztest ${ARGS}
	if [ $? != 0 ]; then
		echo "failing"
		save_ztest_artifacts
		atf_fail "Testcase failed"
	else
		echo "passing"
		atf_pass
	fi
}

#
# ATF Test Program Init Function
#
atf_init_test_cases()
{
	atf_add_test_case ztest
}

save_ztest_artifacts()
{
	# If artifacts_dir is defined, save test artifacts for
	# post-mortem analysis
	if atf_config_has artifacts_dir; then
		TC_ARTIFACTS_DIR=`atf_config_get artifacts_dir`/cddl/usr.bin/ztest/$(atf_get ident)
		mkdir -p $TC_ARTIFACTS_DIR
		TC_CORE_DIR=/var/crash
		if atf_config_has core_dir; then
			TC_CORE_DIR=`atf_config_get core_dir`
		fi
		mv *ztest*.core* $TC_ARTIFACTS_DIR || true
		mv ${TC_CORE_DIR}/*ztest*.core* $TC_ARTIFACTS_DIR || true
	fi
}
