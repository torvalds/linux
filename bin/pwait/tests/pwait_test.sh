# $FreeBSD$

atf_test_case basic
basic_head()
{
	atf_set "descr" "Basic tests on pwait(1) utility"
}

basic_body()
{
	sleep 1 &
	p1=$!

	sleep 5 &
	p5=$!

	sleep 10 &
	p10=$!

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		timeout --preserve-status 15 pwait $p1 $p5 $p10

	atf_check \
		-o empty \
		-e inline:"kill: $p1: No such process\n" \
		-s exit:1 \
		kill -0 $p1

	atf_check \
		-o empty \
		-e inline:"kill: $p5: No such process\n" \
		-s exit:1 \
		kill -0 $p5

	atf_check \
		-o empty \
		-e inline:"kill: $p10: No such process\n" \
		-s exit:1 \
		kill -0 $p10

}

basic_cleanup()
{
	kill $p1 $p5 $p10 >/dev/null 2>&1
	wait $p1 $p5 $p10 >/dev/null 2>&1
}

atf_test_case time_unit
time_unit_head()
{
	atf_set "descr" "Test parsing the timeout unit and value"
}

time_unit_body()
{
	init=1

	atf_check \
		-o empty \
		-e inline:"pwait: timeout unit\n" \
		-s exit:65 \
		timeout --preserve-status 2 pwait -t 1d $init

	atf_check \
		-o empty \
		-e inline:"pwait: timeout unit\n" \
		-s exit:65 \
		timeout --preserve-status 2 pwait -t 1d $init

	atf_check \
		-o empty \
		-e inline:"pwait: timeout value\n" \
		-s exit:65 \
		timeout --preserve-status 2 pwait -t -1 $init

	atf_check \
		-o empty \
		-e inline:"pwait: timeout value\n" \
		-s exit:65 \
		timeout --preserve-status 2 pwait -t 100000001 $init

	# These long duration cases are expected to timeout from the
	# timeout utility rather than pwait -t.
	atf_check \
		-o empty \
		-e empty \
		-s exit:143 \
		timeout --preserve-status 2 pwait -t 100000000 $init

	atf_check \
		-o empty \
		-e empty \
		-s exit:143 \
		timeout --preserve-status 2 pwait -t 1h $init

	atf_check \
		-o empty \
		-e empty \
		-s exit:143 \
		timeout --preserve-status 2 pwait -t 1.5h $init

	atf_check \
		-o empty \
		-e empty \
		-s exit:143 \
		timeout --preserve-status 2 pwait -t 1m $init

	atf_check \
		-o empty \
		-e empty \
		-s exit:143 \
		timeout --preserve-status 2 pwait -t 1.5m $init

	atf_check \
		-o empty \
		-e empty \
		-s exit:143 \
		timeout --preserve-status 2 pwait -t 0 $init

	# The rest are fast enough that pwait -t is expected to trigger
	# the timeout.
	atf_check \
		-o empty \
		-e empty \
		-s exit:124 \
		timeout --preserve-status 2 pwait -t 1s $init

	atf_check \
		-o empty \
		-e empty \
		-s exit:124 \
		timeout --preserve-status 2 pwait -t 1.5s $init

	atf_check \
		-o empty \
		-e empty \
		-s exit:124 \
		timeout --preserve-status 2 pwait -t 1 $init

	atf_check \
		-o empty \
		-e empty \
		-s exit:124 \
		timeout --preserve-status 2 pwait -t 1.5 $init

	atf_check \
		-o empty \
		-e empty \
		-s exit:124 \
		timeout --preserve-status 2 pwait -t 0.5 $init
}

atf_test_case timeout_trigger_timeout
timeout_trigger_timeout_head()
{
	atf_set "descr" "Test that exceeding the timeout is detected"
}

timeout_trigger_timeout_body()
{
	sleep 10 &
	p10=$!

	atf_check \
		-o empty \
		-e empty \
		-s exit:124 \
		timeout --preserve-status 6.5 pwait -t 5 $p10
}

timeout_trigger_timeout_cleanup()
{
	kill $p10 >/dev/null 2>&1
	wait $p10 >/dev/null 2>&1
}

atf_test_case timeout_no_timeout
timeout_no_timeout_head()
{
	atf_set "descr" "Test that not exceeding the timeout continues to wait"
}

timeout_no_timeout_body()
{
	sleep 10 &
	p10=$!

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		timeout --preserve-status 11.5 pwait -t 12 $p10
}

timeout_no_timeout_cleanup()
{
	kill $p10 >/dev/null 2>&1
	wait $p10 >/dev/null 2>&1
}

atf_test_case timeout_many
timeout_many_head()
{
	atf_set "descr" "Test timeout on many processes"
}

timeout_many_body()
{
	sleep 1 &
	p1=$!

	sleep 5 &
	p5=$!

	sleep 10 &
	p10=$!

	atf_check \
		-o empty \
		-e empty \
		-s exit:124 \
		timeout --preserve-status 7.5 pwait -t 6 $p1 $p5 $p10
}

timeout_many_cleanup()
{
	kill $p1 $p5 $p10 >/dev/null 2>&1
	wait $p1 $p5 $p10 >/dev/null 2>&1
}

atf_init_test_cases()
{
	atf_add_test_case basic
	atf_add_test_case time_unit
	atf_add_test_case timeout_trigger_timeout
	atf_add_test_case timeout_no_timeout
	atf_add_test_case timeout_many
}
