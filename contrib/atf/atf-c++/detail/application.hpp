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

#if !defined(ATF_CXX_DETAIL_APPLICATION_HPP)
#define ATF_CXX_DETAIL_APPLICATION_HPP

#include <ostream>
#include <set>
#include <stdexcept>
#include <string>

namespace atf {
namespace application {

// ------------------------------------------------------------------------
// The "usage_error" class.
// ------------------------------------------------------------------------

class usage_error : public std::runtime_error {
    char m_text[4096];

public:
    usage_error(const char*, ...) throw();
    ~usage_error(void) throw();

    const char* what(void) const throw();
};

// ------------------------------------------------------------------------
// The "option" class.
// ------------------------------------------------------------------------

class option {
    char m_character;
    std::string m_argument;
    std::string m_description;

    friend class app;

public:
    option(char, const std::string&, const std::string&);

    bool operator<(const option&) const;
};

// ------------------------------------------------------------------------
// The "app" class.
// ------------------------------------------------------------------------

class app {
    void process_options(void);
    void usage(std::ostream&);

    bool inited(void);

protected:
    typedef std::set< option > options_set;

    int m_argc;
    char* const* m_argv;

    const char* m_argv0;
    const char* m_prog_name;
    std::string m_description;
    std::string m_manpage;

    options_set options(void);

    // To be redefined.
    virtual std::string specific_args(void) const;
    virtual options_set specific_options(void) const;
    virtual void process_option(int, const char*);
    virtual int main(void) = 0;

public:
    app(const std::string&, const std::string&);
    virtual ~app(void);

    int run(int, char* const*);
};

} // namespace application
} // namespace atf

#endif // !defined(ATF_CXX_DETAIL_APPLICATION_HPP)
