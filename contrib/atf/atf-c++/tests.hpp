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

#if !defined(ATF_CXX_TESTS_HPP)
#define ATF_CXX_TESTS_HPP

#include <map>
#include <memory>
#include <string>

extern "C" {
#include <atf-c/defs.h>
}

namespace atf {
namespace tests {

namespace detail {

class atf_tp_writer {
    std::ostream& m_os;

    bool m_is_first;

public:
    atf_tp_writer(std::ostream&);

    void start_tc(const std::string&);
    void end_tc(void);
    void tc_meta_data(const std::string&, const std::string&);
};

bool match(const std::string&, const std::string&);

} // namespace

// ------------------------------------------------------------------------
// The "vars_map" class.
// ------------------------------------------------------------------------

typedef std::map< std::string, std::string > vars_map;

// ------------------------------------------------------------------------
// The "tc" class.
// ------------------------------------------------------------------------

struct tc_impl;

class tc {
    // Non-copyable.
    tc(const tc&);
    tc& operator=(const tc&);

    std::auto_ptr< tc_impl > pimpl;

protected:
    virtual void head(void);
    virtual void body(void) const = 0;
    virtual void cleanup(void) const;

    void require_prog(const std::string&) const;

    friend struct tc_impl;

public:
    tc(const std::string&, const bool);
    virtual ~tc(void);

    void init(const vars_map&);

    const std::string get_config_var(const std::string&) const;
    const std::string get_config_var(const std::string&, const std::string&)
        const;
    const std::string get_md_var(const std::string&) const;
    const vars_map get_md_vars(void) const;
    bool has_config_var(const std::string&) const;
    bool has_md_var(const std::string&) const;
    void set_md_var(const std::string&, const std::string&);

    void run(const std::string&) const;
    void run_cleanup(void) const;

    // To be called from the child process only.
    static void pass(void) ATF_DEFS_ATTRIBUTE_NORETURN;
    static void fail(const std::string&) ATF_DEFS_ATTRIBUTE_NORETURN;
    static void fail_nonfatal(const std::string&);
    static void skip(const std::string&) ATF_DEFS_ATTRIBUTE_NORETURN;
    static void check_errno(const char*, const int, const int, const char*,
                            const bool);
    static void require_errno(const char*, const int, const int, const char*,
                              const bool);
    static void expect_pass(void);
    static void expect_fail(const std::string&);
    static void expect_exit(const int, const std::string&);
    static void expect_signal(const int, const std::string&);
    static void expect_death(const std::string&);
    static void expect_timeout(const std::string&);
};

} // namespace tests
} // namespace atf

#endif // !defined(ATF_CXX_TESTS_HPP)
