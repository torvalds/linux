// Copyright (c) 2008 The NetBSD Foundation, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
// CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
// IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
// IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "atf-c++/detail/process.hpp"

#include <cstdlib>
#include <cstring>

#include <atf-c++.hpp>

#include "atf-c++/detail/test_helpers.hpp"

// TODO: Testing the fork function is a huge task and I'm afraid of
// copy/pasting tons of stuff from the C version.  I'd rather not do that
// until some code can be shared, which cannot happen until the C++ binding
// is cleaned by a fair amount.  Instead... just rely (at the moment) on
// the system tests for the tools using this module.

// ------------------------------------------------------------------------
// Auxiliary functions.
// ------------------------------------------------------------------------

static
std::size_t
array_size(const char* const* array)
{
    std::size_t size = 0;

    for (const char* const* ptr = array; *ptr != NULL; ptr++)
        size++;

    return size;
}

static
atf::process::status
exec_process_helpers(const atf::tests::tc& tc, const char* helper_name)
{
    using atf::process::exec;

    std::vector< std::string > argv;
    argv.push_back(get_process_helpers_path(tc, true).leaf_name());
    argv.push_back(helper_name);

    return exec(get_process_helpers_path(tc, true),
                atf::process::argv_array(argv),
                atf::process::stream_inherit(),
                atf::process::stream_inherit());
}

// ------------------------------------------------------------------------
// Tests for the "argv_array" type.
// ------------------------------------------------------------------------

ATF_TEST_CASE(argv_array_init_carray);
ATF_TEST_CASE_HEAD(argv_array_init_carray)
{
    set_md_var("descr", "Tests that argv_array is correctly constructed "
               "from a C-style array of strings");
}
ATF_TEST_CASE_BODY(argv_array_init_carray)
{
    {
        const char* const carray[] = { NULL };
        atf::process::argv_array argv(carray);

        ATF_REQUIRE_EQ(argv.size(), 0);
    }

    {
        const char* const carray[] = { "arg0", NULL };
        atf::process::argv_array argv(carray);

        ATF_REQUIRE_EQ(argv.size(), 1);
        ATF_REQUIRE(std::strcmp(argv[0], carray[0]) == 0);
    }

    {
        const char* const carray[] = { "arg0", "arg1", "arg2", NULL };
        atf::process::argv_array argv(carray);

        ATF_REQUIRE_EQ(argv.size(), 3);
        ATF_REQUIRE(std::strcmp(argv[0], carray[0]) == 0);
        ATF_REQUIRE(std::strcmp(argv[1], carray[1]) == 0);
        ATF_REQUIRE(std::strcmp(argv[2], carray[2]) == 0);
    }
}

ATF_TEST_CASE(argv_array_init_col);
ATF_TEST_CASE_HEAD(argv_array_init_col)
{
    set_md_var("descr", "Tests that argv_array is correctly constructed "
               "from a string collection");
}
ATF_TEST_CASE_BODY(argv_array_init_col)
{
    {
        std::vector< std::string > col;
        atf::process::argv_array argv(col);

        ATF_REQUIRE_EQ(argv.size(), 0);
    }

    {
        std::vector< std::string > col;
        col.push_back("arg0");
        atf::process::argv_array argv(col);

        ATF_REQUIRE_EQ(argv.size(), 1);
        ATF_REQUIRE_EQ(argv[0], col[0]);
    }

    {
        std::vector< std::string > col;
        col.push_back("arg0");
        col.push_back("arg1");
        col.push_back("arg2");
        atf::process::argv_array argv(col);

        ATF_REQUIRE_EQ(argv.size(), 3);
        ATF_REQUIRE_EQ(argv[0], col[0]);
        ATF_REQUIRE_EQ(argv[1], col[1]);
        ATF_REQUIRE_EQ(argv[2], col[2]);
    }
}

ATF_TEST_CASE(argv_array_init_empty);
ATF_TEST_CASE_HEAD(argv_array_init_empty)
{
    set_md_var("descr", "Tests that argv_array is correctly constructed "
               "by the default constructor");
}
ATF_TEST_CASE_BODY(argv_array_init_empty)
{
    atf::process::argv_array argv;

    ATF_REQUIRE_EQ(argv.size(), 0);
}

ATF_TEST_CASE(argv_array_init_varargs);
ATF_TEST_CASE_HEAD(argv_array_init_varargs)
{
    set_md_var("descr", "Tests that argv_array is correctly constructed "
               "from a variable list of arguments");
}
ATF_TEST_CASE_BODY(argv_array_init_varargs)
{
    {
        atf::process::argv_array argv("arg0", NULL);

        ATF_REQUIRE_EQ(argv.size(), 1);
        ATF_REQUIRE_EQ(argv[0], std::string("arg0"));
    }

    {
        atf::process::argv_array argv("arg0", "arg1", "arg2", NULL);

        ATF_REQUIRE_EQ(argv.size(), 3);
        ATF_REQUIRE_EQ(argv[0], std::string("arg0"));
        ATF_REQUIRE_EQ(argv[1], std::string("arg1"));
        ATF_REQUIRE_EQ(argv[2], std::string("arg2"));
    }
}

ATF_TEST_CASE(argv_array_assign);
ATF_TEST_CASE_HEAD(argv_array_assign)
{
    set_md_var("descr", "Tests that assigning an argv_array works");
}
ATF_TEST_CASE_BODY(argv_array_assign)
{
    using atf::process::argv_array;

    const char* const carray1[] = { "arg1", NULL };
    const char* const carray2[] = { "arg1", "arg2", NULL };

    std::auto_ptr< argv_array > argv1(new argv_array(carray1));
    std::auto_ptr< argv_array > argv2(new argv_array(carray2));

    *argv2 = *argv1;
    ATF_REQUIRE_EQ(argv2->size(), argv1->size());
    ATF_REQUIRE(std::strcmp((*argv2)[0], (*argv1)[0]) == 0);

    ATF_REQUIRE(argv2->exec_argv() != argv1->exec_argv());
    argv1.release();
    {
        const char* const* eargv2 = argv2->exec_argv();
        ATF_REQUIRE(std::strcmp(eargv2[0], carray1[0]) == 0);
        ATF_REQUIRE_EQ(eargv2[1], static_cast< const char* >(NULL));
    }

    argv2.release();
}

ATF_TEST_CASE(argv_array_copy);
ATF_TEST_CASE_HEAD(argv_array_copy)
{
    set_md_var("descr", "Tests that copying an argv_array constructed from "
               "a C-style array of strings works");
}
ATF_TEST_CASE_BODY(argv_array_copy)
{
    using atf::process::argv_array;

    const char* const carray[] = { "arg0", NULL };

    std::auto_ptr< argv_array > argv1(new argv_array(carray));
    std::auto_ptr< argv_array > argv2(new argv_array(*argv1));

    ATF_REQUIRE_EQ(argv2->size(), argv1->size());
    ATF_REQUIRE(std::strcmp((*argv2)[0], (*argv1)[0]) == 0);

    ATF_REQUIRE(argv2->exec_argv() != argv1->exec_argv());
    argv1.release();
    {
        const char* const* eargv2 = argv2->exec_argv();
        ATF_REQUIRE(std::strcmp(eargv2[0], carray[0]) == 0);
        ATF_REQUIRE_EQ(eargv2[1], static_cast< const char* >(NULL));
    }

    argv2.release();
}

ATF_TEST_CASE(argv_array_exec_argv);
ATF_TEST_CASE_HEAD(argv_array_exec_argv)
{
    set_md_var("descr", "Tests that the exec argv provided by an argv_array "
               "is correct");
}
ATF_TEST_CASE_BODY(argv_array_exec_argv)
{
    using atf::process::argv_array;

    {
        argv_array argv;
        const char* const* eargv = argv.exec_argv();
        ATF_REQUIRE_EQ(array_size(eargv), 0);
        ATF_REQUIRE_EQ(eargv[0], static_cast< const char* >(NULL));
    }

    {
        const char* const carray[] = { "arg0", NULL };
        argv_array argv(carray);
        const char* const* eargv = argv.exec_argv();
        ATF_REQUIRE_EQ(array_size(eargv), 1);
        ATF_REQUIRE(std::strcmp(eargv[0], "arg0") == 0);
        ATF_REQUIRE_EQ(eargv[1], static_cast< const char* >(NULL));
    }

    {
        std::vector< std::string > col;
        col.push_back("arg0");
        argv_array argv(col);
        const char* const* eargv = argv.exec_argv();
        ATF_REQUIRE_EQ(array_size(eargv), 1);
        ATF_REQUIRE(std::strcmp(eargv[0], "arg0") == 0);
        ATF_REQUIRE_EQ(eargv[1], static_cast< const char* >(NULL));
    }
}

ATF_TEST_CASE(argv_array_iter);
ATF_TEST_CASE_HEAD(argv_array_iter)
{
    set_md_var("descr", "Tests that an argv_array can be iterated");
}
ATF_TEST_CASE_BODY(argv_array_iter)
{
    using atf::process::argv_array;

    std::vector< std::string > vector;
    vector.push_back("arg0");
    vector.push_back("arg1");
    vector.push_back("arg2");

    argv_array argv(vector);
    ATF_REQUIRE_EQ(argv.size(), 3);
    std::vector< std::string >::size_type pos = 0;
    for (argv_array::const_iterator iter = argv.begin(); iter != argv.end();
         iter++) {
        ATF_REQUIRE_EQ(*iter, vector[pos]);
        pos++;
    }
}

// ------------------------------------------------------------------------
// Tests cases for the free functions.
// ------------------------------------------------------------------------

ATF_TEST_CASE(exec_failure);
ATF_TEST_CASE_HEAD(exec_failure)
{
    set_md_var("descr", "Tests execing a command that reports failure");
}
ATF_TEST_CASE_BODY(exec_failure)
{
    const atf::process::status s = exec_process_helpers(*this, "exit-failure");
    ATF_REQUIRE(s.exited());
    ATF_REQUIRE_EQ(s.exitstatus(), EXIT_FAILURE);
}

ATF_TEST_CASE(exec_success);
ATF_TEST_CASE_HEAD(exec_success)
{
    set_md_var("descr", "Tests execing a command that reports success");
}
ATF_TEST_CASE_BODY(exec_success)
{
    const atf::process::status s = exec_process_helpers(*this, "exit-success");
    ATF_REQUIRE(s.exited());
    ATF_REQUIRE_EQ(s.exitstatus(), EXIT_SUCCESS);
}

// ------------------------------------------------------------------------
// Main.
// ------------------------------------------------------------------------

ATF_INIT_TEST_CASES(tcs)
{
    // Add the test cases for the "argv_array" type.
    ATF_ADD_TEST_CASE(tcs, argv_array_assign);
    ATF_ADD_TEST_CASE(tcs, argv_array_copy);
    ATF_ADD_TEST_CASE(tcs, argv_array_exec_argv);
    ATF_ADD_TEST_CASE(tcs, argv_array_init_carray);
    ATF_ADD_TEST_CASE(tcs, argv_array_init_col);
    ATF_ADD_TEST_CASE(tcs, argv_array_init_empty);
    ATF_ADD_TEST_CASE(tcs, argv_array_init_varargs);
    ATF_ADD_TEST_CASE(tcs, argv_array_iter);

    // Add the test cases for the free functions.
    ATF_ADD_TEST_CASE(tcs, exec_failure);
    ATF_ADD_TEST_CASE(tcs, exec_success);
}
