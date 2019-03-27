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

#include "atf-c++/tests.hpp"

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
}

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

extern "C" {
#include "atf-c/error.h"
#include "atf-c/tc.h"
#include "atf-c/utils.h"
}

#include "atf-c++/detail/application.hpp"
#include "atf-c++/detail/auto_array.hpp"
#include "atf-c++/detail/env.hpp"
#include "atf-c++/detail/exceptions.hpp"
#include "atf-c++/detail/fs.hpp"
#include "atf-c++/detail/sanity.hpp"
#include "atf-c++/detail/text.hpp"

#if defined(HAVE_GNU_GETOPT)
#   define GETOPT_POSIX "+"
#else
#   define GETOPT_POSIX ""
#endif

namespace impl = atf::tests;
namespace detail = atf::tests::detail;
#define IMPL_NAME "atf::tests"

using atf::application::usage_error;

// ------------------------------------------------------------------------
// The "atf_tp_writer" class.
// ------------------------------------------------------------------------

detail::atf_tp_writer::atf_tp_writer(std::ostream& os) :
    m_os(os),
    m_is_first(true)
{
    m_os << "Content-Type: application/X-atf-tp; version=\"1\"\n\n";
}

void
detail::atf_tp_writer::start_tc(const std::string& ident)
{
    if (!m_is_first)
        m_os << "\n";
    m_os << "ident: " << ident << "\n";
    m_os.flush();
}

void
detail::atf_tp_writer::end_tc(void)
{
    if (m_is_first)
        m_is_first = false;
}

void
detail::atf_tp_writer::tc_meta_data(const std::string& name,
                                    const std::string& value)
{
    PRE(name != "ident");
    m_os << name << ": " << value << "\n";
    m_os.flush();
}

// ------------------------------------------------------------------------
// Free helper functions.
// ------------------------------------------------------------------------

std::string Program_Name;

static void
set_program_name(const char* argv0)
{
    const std::string program_name = atf::fs::path(argv0).leaf_name();
    // Libtool workaround: if running from within the source tree (binaries
    // that are not installed yet), skip the "lt-" prefix added to files in
    // the ".libs" directory to show the real (not temporary) name.
    if (program_name.substr(0, 3) == "lt-")
        Program_Name = program_name.substr(3);
    else
        Program_Name = program_name;
}

bool
detail::match(const std::string& regexp, const std::string& str)
{
    return atf::text::match(str, regexp);
}

// ------------------------------------------------------------------------
// The "tc" class.
// ------------------------------------------------------------------------

static std::map< atf_tc_t*, impl::tc* > wraps;
static std::map< const atf_tc_t*, const impl::tc* > cwraps;

struct impl::tc_impl {
private:
    // Non-copyable.
    tc_impl(const tc_impl&);
    tc_impl& operator=(const tc_impl&);

public:
    std::string m_ident;
    atf_tc_t m_tc;
    bool m_has_cleanup;

    tc_impl(const std::string& ident, const bool has_cleanup) :
        m_ident(ident),
        m_has_cleanup(has_cleanup)
    {
    }

    static void
    wrap_head(atf_tc_t *tc)
    {
        std::map< atf_tc_t*, impl::tc* >::iterator iter = wraps.find(tc);
        INV(iter != wraps.end());
        (*iter).second->head();
    }

    static void
    wrap_body(const atf_tc_t *tc)
    {
        std::map< const atf_tc_t*, const impl::tc* >::const_iterator iter =
            cwraps.find(tc);
        INV(iter != cwraps.end());
        (*iter).second->body();
    }

    static void
    wrap_cleanup(const atf_tc_t *tc)
    {
        std::map< const atf_tc_t*, const impl::tc* >::const_iterator iter =
            cwraps.find(tc);
        INV(iter != cwraps.end());
        (*iter).second->cleanup();
    }
};

impl::tc::tc(const std::string& ident, const bool has_cleanup) :
    pimpl(new tc_impl(ident, has_cleanup))
{
}

impl::tc::~tc(void)
{
    cwraps.erase(&pimpl->m_tc);
    wraps.erase(&pimpl->m_tc);

    atf_tc_fini(&pimpl->m_tc);
}

void
impl::tc::init(const vars_map& config)
{
    atf_error_t err;

    auto_array< const char * > array(new const char*[(config.size() * 2) + 1]);
    const char **ptr = array.get();
    for (vars_map::const_iterator iter = config.begin();
         iter != config.end(); iter++) {
         *ptr = (*iter).first.c_str();
         *(ptr + 1) = (*iter).second.c_str();
         ptr += 2;
    }
    *ptr = NULL;

    wraps[&pimpl->m_tc] = this;
    cwraps[&pimpl->m_tc] = this;

    err = atf_tc_init(&pimpl->m_tc, pimpl->m_ident.c_str(), pimpl->wrap_head,
        pimpl->wrap_body, pimpl->m_has_cleanup ? pimpl->wrap_cleanup : NULL,
        array.get());
    if (atf_is_error(err))
        throw_atf_error(err);
}

bool
impl::tc::has_config_var(const std::string& var)
    const
{
    return atf_tc_has_config_var(&pimpl->m_tc, var.c_str());
}

bool
impl::tc::has_md_var(const std::string& var)
    const
{
    return atf_tc_has_md_var(&pimpl->m_tc, var.c_str());
}

const std::string
impl::tc::get_config_var(const std::string& var)
    const
{
    return atf_tc_get_config_var(&pimpl->m_tc, var.c_str());
}

const std::string
impl::tc::get_config_var(const std::string& var, const std::string& defval)
    const
{
    return atf_tc_get_config_var_wd(&pimpl->m_tc, var.c_str(), defval.c_str());
}

const std::string
impl::tc::get_md_var(const std::string& var)
    const
{
    return atf_tc_get_md_var(&pimpl->m_tc, var.c_str());
}

const impl::vars_map
impl::tc::get_md_vars(void)
    const
{
    vars_map vars;

    char **array = atf_tc_get_md_vars(&pimpl->m_tc);
    try {
        char **ptr;
        for (ptr = array; *ptr != NULL; ptr += 2)
            vars[*ptr] = *(ptr + 1);
    } catch (...) {
        atf_utils_free_charpp(array);
        throw;
    }

    return vars;
}

void
impl::tc::set_md_var(const std::string& var, const std::string& val)
{
    atf_error_t err = atf_tc_set_md_var(&pimpl->m_tc, var.c_str(), val.c_str());
    if (atf_is_error(err))
        throw_atf_error(err);
}

void
impl::tc::run(const std::string& resfile)
    const
{
    atf_error_t err = atf_tc_run(&pimpl->m_tc, resfile.c_str());
    if (atf_is_error(err))
        throw_atf_error(err);
}

void
impl::tc::run_cleanup(void)
    const
{
    atf_error_t err = atf_tc_cleanup(&pimpl->m_tc);
    if (atf_is_error(err))
        throw_atf_error(err);
}

void
impl::tc::head(void)
{
}

void
impl::tc::cleanup(void)
    const
{
}

void
impl::tc::require_prog(const std::string& prog)
    const
{
    atf_tc_require_prog(prog.c_str());
}

void
impl::tc::pass(void)
{
    atf_tc_pass();
}

void
impl::tc::fail(const std::string& reason)
{
    atf_tc_fail("%s", reason.c_str());
}

void
impl::tc::fail_nonfatal(const std::string& reason)
{
    atf_tc_fail_nonfatal("%s", reason.c_str());
}

void
impl::tc::skip(const std::string& reason)
{
    atf_tc_skip("%s", reason.c_str());
}

void
impl::tc::check_errno(const char* file, const int line, const int exp_errno,
                      const char* expr_str, const bool result)
{
    atf_tc_check_errno(file, line, exp_errno, expr_str, result);
}

void
impl::tc::require_errno(const char* file, const int line, const int exp_errno,
                        const char* expr_str, const bool result)
{
    atf_tc_require_errno(file, line, exp_errno, expr_str, result);
}

void
impl::tc::expect_pass(void)
{
    atf_tc_expect_pass();
}

void
impl::tc::expect_fail(const std::string& reason)
{
    atf_tc_expect_fail("%s", reason.c_str());
}

void
impl::tc::expect_exit(const int exitcode, const std::string& reason)
{
    atf_tc_expect_exit(exitcode, "%s", reason.c_str());
}

void
impl::tc::expect_signal(const int signo, const std::string& reason)
{
    atf_tc_expect_signal(signo, "%s", reason.c_str());
}

void
impl::tc::expect_death(const std::string& reason)
{
    atf_tc_expect_death("%s", reason.c_str());
}

void
impl::tc::expect_timeout(const std::string& reason)
{
    atf_tc_expect_timeout("%s", reason.c_str());
}

// ------------------------------------------------------------------------
// Test program main code.
// ------------------------------------------------------------------------

namespace {

typedef std::vector< impl::tc * > tc_vector;

enum tc_part { BODY, CLEANUP };

static void
parse_vflag(const std::string& str, atf::tests::vars_map& vars)
{
    if (str.empty())
        throw std::runtime_error("-v requires a non-empty argument");

    std::vector< std::string > ws = atf::text::split(str, "=");
    if (ws.size() == 1 && str[str.length() - 1] == '=') {
        vars[ws[0]] = "";
    } else {
        if (ws.size() != 2)
            throw std::runtime_error("-v requires an argument of the form "
                                     "var=value");

        vars[ws[0]] = ws[1];
    }
}

static atf::fs::path
handle_srcdir(const char* argv0, const std::string& srcdir_arg)
{
    atf::fs::path srcdir(".");

    if (srcdir_arg.empty()) {
        srcdir = atf::fs::path(argv0).branch_path();
        if (srcdir.leaf_name() == ".libs")
            srcdir = srcdir.branch_path();
    } else
        srcdir = atf::fs::path(srcdir_arg);

    if (!atf::fs::exists(srcdir / Program_Name))
        throw usage_error("Cannot find the test program in the source "
                          "directory `%s'", srcdir.c_str());

    if (!srcdir.is_absolute())
        srcdir = srcdir.to_absolute();

    return srcdir;
}

static void
init_tcs(void (*add_tcs)(tc_vector&), tc_vector& tcs,
         const atf::tests::vars_map& vars)
{
    add_tcs(tcs);
    for (tc_vector::iterator iter = tcs.begin(); iter != tcs.end(); iter++) {
        impl::tc* tc = *iter;

        tc->init(vars);
    }
}

static int
list_tcs(const tc_vector& tcs)
{
    detail::atf_tp_writer writer(std::cout);

    for (tc_vector::const_iterator iter = tcs.begin();
         iter != tcs.end(); iter++) {
        const impl::vars_map vars = (*iter)->get_md_vars();

        {
            impl::vars_map::const_iterator iter2 = vars.find("ident");
            INV(iter2 != vars.end());
            writer.start_tc((*iter2).second);
        }

        for (impl::vars_map::const_iterator iter2 = vars.begin();
             iter2 != vars.end(); iter2++) {
            const std::string& key = (*iter2).first;
            if (key != "ident")
                writer.tc_meta_data(key, (*iter2).second);
        }

        writer.end_tc();
    }

    return EXIT_SUCCESS;
}

static impl::tc*
find_tc(tc_vector tcs, const std::string& name)
{
    std::vector< std::string > ids;
    for (tc_vector::iterator iter = tcs.begin();
         iter != tcs.end(); iter++) {
        impl::tc* tc = *iter;

        if (tc->get_md_var("ident") == name)
            return tc;
    }
    throw usage_error("Unknown test case `%s'", name.c_str());
}

static std::pair< std::string, tc_part >
process_tcarg(const std::string& tcarg)
{
    const std::string::size_type pos = tcarg.find(':');
    if (pos == std::string::npos) {
        return std::make_pair(tcarg, BODY);
    } else {
        const std::string tcname = tcarg.substr(0, pos);

        const std::string partname = tcarg.substr(pos + 1);
        if (partname == "body")
            return std::make_pair(tcname, BODY);
        else if (partname == "cleanup")
            return std::make_pair(tcname, CLEANUP);
        else {
            throw usage_error("Invalid test case part `%s'", partname.c_str());
        }
    }
}

static int
run_tc(tc_vector& tcs, const std::string& tcarg, const atf::fs::path& resfile)
{
    const std::pair< std::string, tc_part > fields = process_tcarg(tcarg);

    impl::tc* tc = find_tc(tcs, fields.first);

    if (!atf::env::has("__RUNNING_INSIDE_ATF_RUN") || atf::env::get(
        "__RUNNING_INSIDE_ATF_RUN") != "internal-yes-value")
    {
        std::cerr << Program_Name << ": WARNING: Running test cases outside "
            "of kyua(1) is unsupported\n";
        std::cerr << Program_Name << ": WARNING: No isolation nor timeout "
            "control is being applied; you may get unexpected failures; see "
            "atf-test-case(4)\n";
    }

    switch (fields.second) {
    case BODY:
        tc->run(resfile.str());
        break;
    case CLEANUP:
        tc->run_cleanup();
        break;
    default:
        UNREACHABLE;
    }
    return EXIT_SUCCESS;
}

static int
safe_main(int argc, char** argv, void (*add_tcs)(tc_vector&))
{
    const char* argv0 = argv[0];

    bool lflag = false;
    atf::fs::path resfile("/dev/stdout");
    std::string srcdir_arg;
    atf::tests::vars_map vars;

    int ch;
    int old_opterr;

    old_opterr = opterr;
    ::opterr = 0;
    while ((ch = ::getopt(argc, argv, GETOPT_POSIX ":lr:s:v:")) != -1) {
        switch (ch) {
        case 'l':
            lflag = true;
            break;

        case 'r':
            resfile = atf::fs::path(::optarg);
            break;

        case 's':
            srcdir_arg = ::optarg;
            break;

        case 'v':
            parse_vflag(::optarg, vars);
            break;

        case ':':
            throw usage_error("Option -%c requires an argument.", ::optopt);
            break;

        case '?':
        default:
            throw usage_error("Unknown option -%c.", ::optopt);
        }
    }
    argc -= optind;
    argv += optind;

    // Clear getopt state just in case the test wants to use it.
    ::opterr = old_opterr;
    ::optind = 1;
#if defined(HAVE_OPTRESET)
    ::optreset = 1;
#endif

    vars["srcdir"] = handle_srcdir(argv0, srcdir_arg).str();

    int errcode;

    tc_vector tcs;
    if (lflag) {
        if (argc > 0)
            throw usage_error("Cannot provide test case names with -l");

        init_tcs(add_tcs, tcs, vars);
        errcode = list_tcs(tcs);
    } else {
        if (argc == 0)
            throw usage_error("Must provide a test case name");
        else if (argc > 1)
            throw usage_error("Cannot provide more than one test case name");
        INV(argc == 1);

        init_tcs(add_tcs, tcs, vars);
        errcode = run_tc(tcs, argv[0], resfile);
    }
    for (tc_vector::iterator iter = tcs.begin(); iter != tcs.end(); iter++) {
        impl::tc* tc = *iter;

        delete tc;
    }

    return errcode;
}

}  // anonymous namespace

namespace atf {
    namespace tests {
        int run_tp(int, char**, void (*)(tc_vector&));
    }
}

int
impl::run_tp(int argc, char** argv, void (*add_tcs)(tc_vector&))
{
    try {
        set_program_name(argv[0]);
        return ::safe_main(argc, argv, add_tcs);
    } catch (const usage_error& e) {
        std::cerr
            << Program_Name << ": ERROR: " << e.what() << '\n'
            << Program_Name << ": See atf-test-program(1) for usage details.\n";
        return EXIT_FAILURE;
    }
}
