// Copyright (c) 2007 The NetBSD Foundation, Inc.
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

#include "atf-c++/check.hpp"

extern "C" {
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
}

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <vector>

#include <atf-c++.hpp>

#include "atf-c++/detail/fs.hpp"
#include "atf-c++/detail/process.hpp"
#include "atf-c++/detail/test_helpers.hpp"
#include "atf-c++/detail/text.hpp"
#include "atf-c++/utils.hpp"

// ------------------------------------------------------------------------
// Auxiliary functions.
// ------------------------------------------------------------------------

static
std::auto_ptr< atf::check::check_result >
do_exec(const atf::tests::tc* tc, const char* helper_name)
{
    std::vector< std::string > argv;
    argv.push_back(get_process_helpers_path(*tc, false).str());
    argv.push_back(helper_name);
    std::cout << "Executing " << argv[0] << " " << argv[1] << "\n";

    atf::process::argv_array argva(argv);
    return atf::check::exec(argva);
}

static
std::auto_ptr< atf::check::check_result >
do_exec(const atf::tests::tc* tc, const char* helper_name, const char *carg2)
{
    std::vector< std::string > argv;
    argv.push_back(get_process_helpers_path(*tc, false).str());
    argv.push_back(helper_name);
    argv.push_back(carg2);
    std::cout << "Executing " << argv[0] << " " << argv[1] << " "
              << argv[2] << "\n";

    atf::process::argv_array argva(argv);
    return atf::check::exec(argva);
}

// ------------------------------------------------------------------------
// Helper test cases for the free functions.
// ------------------------------------------------------------------------

ATF_TEST_CASE(h_build_c_o_ok);
ATF_TEST_CASE_HEAD(h_build_c_o_ok)
{
    set_md_var("descr", "Helper test case for build_c_o");
}
ATF_TEST_CASE_BODY(h_build_c_o_ok)
{
    std::ofstream sfile("test.c");
    sfile << "#include <stdio.h>\n";
    sfile.close();

    ATF_REQUIRE(atf::check::build_c_o("test.c", "test.o",
                                      atf::process::argv_array()));
}

ATF_TEST_CASE(h_build_c_o_fail);
ATF_TEST_CASE_HEAD(h_build_c_o_fail)
{
    set_md_var("descr", "Helper test case for build_c_o");
}
ATF_TEST_CASE_BODY(h_build_c_o_fail)
{
    std::ofstream sfile("test.c");
    sfile << "void foo(void) { int a = UNDEFINED_SYMBOL; }\n";
    sfile.close();

    ATF_REQUIRE(!atf::check::build_c_o("test.c", "test.o",
                                       atf::process::argv_array()));
}

ATF_TEST_CASE(h_build_cpp_ok);
ATF_TEST_CASE_HEAD(h_build_cpp_ok)
{
    set_md_var("descr", "Helper test case for build_cpp");
}
ATF_TEST_CASE_BODY(h_build_cpp_ok)
{
    std::ofstream sfile("test.c");
    sfile << "#define A foo\n";
    sfile << "#define B bar\n";
    sfile << "A B\n";
    sfile.close();

    ATF_REQUIRE(atf::check::build_cpp("test.c", "test.p",
                                      atf::process::argv_array()));
}

ATF_TEST_CASE(h_build_cpp_fail);
ATF_TEST_CASE_HEAD(h_build_cpp_fail)
{
    set_md_var("descr", "Helper test case for build_cpp");
}
ATF_TEST_CASE_BODY(h_build_cpp_fail)
{
    std::ofstream sfile("test.c");
    sfile << "#include \"./non-existent.h\"\n";
    sfile.close();

    ATF_REQUIRE(!atf::check::build_cpp("test.c", "test.p",
                                       atf::process::argv_array()));
}

ATF_TEST_CASE(h_build_cxx_o_ok);
ATF_TEST_CASE_HEAD(h_build_cxx_o_ok)
{
    set_md_var("descr", "Helper test case for build_cxx_o");
}
ATF_TEST_CASE_BODY(h_build_cxx_o_ok)
{
    std::ofstream sfile("test.cpp");
    sfile << "#include <iostream>\n";
    sfile.close();

    ATF_REQUIRE(atf::check::build_cxx_o("test.cpp", "test.o",
                                        atf::process::argv_array()));
}

ATF_TEST_CASE(h_build_cxx_o_fail);
ATF_TEST_CASE_HEAD(h_build_cxx_o_fail)
{
    set_md_var("descr", "Helper test case for build_cxx_o");
}
ATF_TEST_CASE_BODY(h_build_cxx_o_fail)
{
    std::ofstream sfile("test.cpp");
    sfile << "void foo(void) { int a = UNDEFINED_SYMBOL; }\n";
    sfile.close();

    ATF_REQUIRE(!atf::check::build_cxx_o("test.cpp", "test.o",
                                         atf::process::argv_array()));
}

// ------------------------------------------------------------------------
// Test cases for the free functions.
// ------------------------------------------------------------------------

ATF_TEST_CASE(build_c_o);
ATF_TEST_CASE_HEAD(build_c_o)
{
    set_md_var("descr", "Tests the build_c_o function");
}
ATF_TEST_CASE_BODY(build_c_o)
{
    ATF_TEST_CASE_USE(h_build_c_o_ok);
    run_h_tc< ATF_TEST_CASE_NAME(h_build_c_o_ok) >();
    ATF_REQUIRE(atf::utils::grep_file("-o test.o", "stdout"));
    ATF_REQUIRE(atf::utils::grep_file("-c test.c", "stdout"));

    ATF_TEST_CASE_USE(h_build_c_o_fail);
    run_h_tc< ATF_TEST_CASE_NAME(h_build_c_o_fail) >();
    ATF_REQUIRE(atf::utils::grep_file("-o test.o", "stdout"));
    ATF_REQUIRE(atf::utils::grep_file("-c test.c", "stdout"));
    ATF_REQUIRE(atf::utils::grep_file("test.c", "stderr"));
    ATF_REQUIRE(atf::utils::grep_file("UNDEFINED_SYMBOL", "stderr"));
}

ATF_TEST_CASE(build_cpp);
ATF_TEST_CASE_HEAD(build_cpp)
{
    set_md_var("descr", "Tests the build_cpp function");
}
ATF_TEST_CASE_BODY(build_cpp)
{
    ATF_TEST_CASE_USE(h_build_cpp_ok);
    run_h_tc< ATF_TEST_CASE_NAME(h_build_cpp_ok) >();
    ATF_REQUIRE(atf::utils::grep_file("-o.*test.p", "stdout"));
    ATF_REQUIRE(atf::utils::grep_file("test.c", "stdout"));
    ATF_REQUIRE(atf::utils::grep_file("foo bar", "test.p"));

    ATF_TEST_CASE_USE(h_build_cpp_fail);
    run_h_tc< ATF_TEST_CASE_NAME(h_build_cpp_fail) >();
    ATF_REQUIRE(atf::utils::grep_file("-o test.p", "stdout"));
    ATF_REQUIRE(atf::utils::grep_file("test.c", "stdout"));
    ATF_REQUIRE(atf::utils::grep_file("test.c", "stderr"));
    ATF_REQUIRE(atf::utils::grep_file("non-existent.h", "stderr"));
}

ATF_TEST_CASE(build_cxx_o);
ATF_TEST_CASE_HEAD(build_cxx_o)
{
    set_md_var("descr", "Tests the build_cxx_o function");
}
ATF_TEST_CASE_BODY(build_cxx_o)
{
    ATF_TEST_CASE_USE(h_build_cxx_o_ok);
    run_h_tc< ATF_TEST_CASE_NAME(h_build_cxx_o_ok) >();
    ATF_REQUIRE(atf::utils::grep_file("-o test.o", "stdout"));
    ATF_REQUIRE(atf::utils::grep_file("-c test.cpp", "stdout"));

    ATF_TEST_CASE_USE(h_build_cxx_o_fail);
    run_h_tc< ATF_TEST_CASE_NAME(h_build_cxx_o_fail) >();
    ATF_REQUIRE(atf::utils::grep_file("-o test.o", "stdout"));
    ATF_REQUIRE(atf::utils::grep_file("-c test.cpp", "stdout"));
    ATF_REQUIRE(atf::utils::grep_file("test.cpp", "stderr"));
    ATF_REQUIRE(atf::utils::grep_file("UNDEFINED_SYMBOL", "stderr"));
}

ATF_TEST_CASE(exec_cleanup);
ATF_TEST_CASE_HEAD(exec_cleanup)
{
    set_md_var("descr", "Tests that exec properly cleans up the temporary "
               "files it creates");
}
ATF_TEST_CASE_BODY(exec_cleanup)
{
    std::auto_ptr< atf::fs::path > out;
    std::auto_ptr< atf::fs::path > err;

    {
        std::auto_ptr< atf::check::check_result > r =
            do_exec(this, "exit-success");
        out.reset(new atf::fs::path(r->stdout_path()));
        err.reset(new atf::fs::path(r->stderr_path()));
        ATF_REQUIRE(atf::fs::exists(*out.get()));
        ATF_REQUIRE(atf::fs::exists(*err.get()));
    }
    ATF_REQUIRE(!atf::fs::exists(*out.get()));
    ATF_REQUIRE(!atf::fs::exists(*err.get()));
}

ATF_TEST_CASE(exec_exitstatus);
ATF_TEST_CASE_HEAD(exec_exitstatus)
{
    set_md_var("descr", "Tests that exec properly captures the exit "
               "status of the executed command");
}
ATF_TEST_CASE_BODY(exec_exitstatus)
{
    {
        std::auto_ptr< atf::check::check_result > r =
            do_exec(this, "exit-success");
        ATF_REQUIRE(r->exited());
        ATF_REQUIRE(!r->signaled());
        ATF_REQUIRE_EQ(r->exitcode(), EXIT_SUCCESS);
    }

    {
        std::auto_ptr< atf::check::check_result > r =
            do_exec(this, "exit-failure");
        ATF_REQUIRE(r->exited());
        ATF_REQUIRE(!r->signaled());
        ATF_REQUIRE_EQ(r->exitcode(), EXIT_FAILURE);
    }

    {
        std::auto_ptr< atf::check::check_result > r =
            do_exec(this, "exit-signal");
        ATF_REQUIRE(!r->exited());
        ATF_REQUIRE(r->signaled());
        ATF_REQUIRE_EQ(r->termsig(), SIGKILL);
    }
}

static
void
check_lines(const std::string& path, const char* outname,
            const char* resname)
{
    std::ifstream f(path.c_str());
    ATF_REQUIRE(f);

    std::string line;
    std::getline(f, line);
    ATF_REQUIRE_EQ(line, std::string("Line 1 to ") + outname + " for " +
                    resname);
    std::getline(f, line);
    ATF_REQUIRE_EQ(line, std::string("Line 2 to ") + outname + " for " +
                    resname);
}

ATF_TEST_CASE(exec_stdout_stderr);
ATF_TEST_CASE_HEAD(exec_stdout_stderr)
{
    set_md_var("descr", "Tests that exec properly captures the stdout "
               "and stderr streams of the child process");
}
ATF_TEST_CASE_BODY(exec_stdout_stderr)
{
    std::auto_ptr< atf::check::check_result > r1 =
        do_exec(this, "stdout-stderr", "result1");
    ATF_REQUIRE(r1->exited());
    ATF_REQUIRE_EQ(r1->exitcode(), EXIT_SUCCESS);

    std::auto_ptr< atf::check::check_result > r2 =
        do_exec(this, "stdout-stderr", "result2");
    ATF_REQUIRE(r2->exited());
    ATF_REQUIRE_EQ(r2->exitcode(), EXIT_SUCCESS);

    const std::string out1 = r1->stdout_path();
    const std::string out2 = r2->stdout_path();
    const std::string err1 = r1->stderr_path();
    const std::string err2 = r2->stderr_path();

    ATF_REQUIRE(out1.find("check.XXXXXX") == std::string::npos);
    ATF_REQUIRE(out2.find("check.XXXXXX") == std::string::npos);
    ATF_REQUIRE(err1.find("check.XXXXXX") == std::string::npos);
    ATF_REQUIRE(err2.find("check.XXXXXX") == std::string::npos);

    ATF_REQUIRE(out1.find("/check") != std::string::npos);
    ATF_REQUIRE(out2.find("/check") != std::string::npos);
    ATF_REQUIRE(err1.find("/check") != std::string::npos);
    ATF_REQUIRE(err2.find("/check") != std::string::npos);

    ATF_REQUIRE(out1.find("/stdout") != std::string::npos);
    ATF_REQUIRE(out2.find("/stdout") != std::string::npos);
    ATF_REQUIRE(err1.find("/stderr") != std::string::npos);
    ATF_REQUIRE(err2.find("/stderr") != std::string::npos);

    ATF_REQUIRE(out1 != out2);
    ATF_REQUIRE(err1 != err2);

    check_lines(out1, "stdout", "result1");
    check_lines(out2, "stdout", "result2");
    check_lines(err1, "stderr", "result1");
    check_lines(err2, "stderr", "result2");
}

ATF_TEST_CASE(exec_unknown);
ATF_TEST_CASE_HEAD(exec_unknown)
{
    set_md_var("descr", "Tests that running a non-existing binary "
               "is handled correctly");
}
ATF_TEST_CASE_BODY(exec_unknown)
{
    std::vector< std::string > argv;
    argv.push_back("/foo/bar/non-existent");

    atf::process::argv_array argva(argv);
    std::auto_ptr< atf::check::check_result > r = atf::check::exec(argva);
    ATF_REQUIRE(r->exited());
    ATF_REQUIRE_EQ(r->exitcode(), 127);
}

// ------------------------------------------------------------------------
// Main.
// ------------------------------------------------------------------------

ATF_INIT_TEST_CASES(tcs)
{
    // Add the test cases for the free functions.
    ATF_ADD_TEST_CASE(tcs, build_c_o);
    ATF_ADD_TEST_CASE(tcs, build_cpp);
    ATF_ADD_TEST_CASE(tcs, build_cxx_o);
    ATF_ADD_TEST_CASE(tcs, exec_cleanup);
    ATF_ADD_TEST_CASE(tcs, exec_exitstatus);
    ATF_ADD_TEST_CASE(tcs, exec_stdout_stderr);
    ATF_ADD_TEST_CASE(tcs, exec_unknown);
}
