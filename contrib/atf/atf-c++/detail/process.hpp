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

#if !defined(ATF_CXX_DETAIL_PROCESS_HPP)
#define ATF_CXX_DETAIL_PROCESS_HPP

extern "C" {
#include <sys/types.h>

#include <atf-c/detail/process.h>
#include <atf-c/error.h>
}

#include <string>
#include <vector>

#include <atf-c++/detail/auto_array.hpp>
#include <atf-c++/detail/exceptions.hpp>
#include <atf-c++/detail/fs.hpp>

namespace atf {
namespace process {

class child;
class status;

// ------------------------------------------------------------------------
// The "argv_array" type.
// ------------------------------------------------------------------------

class argv_array {
    typedef std::vector< std::string > args_vector;
    args_vector m_args;

    // TODO: This is immutable, so we should be able to use
    // std::tr1::shared_array instead when it becomes widely available.
    // The reason would be to remove all copy constructors and assignment
    // operators from this class.
    auto_array< const char* > m_exec_argv;
    void ctor_init_exec_argv(void);

public:
    typedef args_vector::const_iterator const_iterator;
    typedef args_vector::size_type size_type;

    argv_array(void);
    argv_array(const char*, ...);
    explicit argv_array(const char* const*);
    template< class C > explicit argv_array(const C&);
    argv_array(const argv_array&);

    const char* const* exec_argv(void) const;
    size_type size(void) const;
    const char* operator[](int) const;

    const_iterator begin(void) const;
    const_iterator end(void) const;

    argv_array& operator=(const argv_array&);
};

template< class C >
argv_array::argv_array(const C& c)
{
    for (typename C::const_iterator iter = c.begin(); iter != c.end();
         iter++)
        m_args.push_back(*iter);
    ctor_init_exec_argv();
}

// ------------------------------------------------------------------------
// The "stream" types.
// ------------------------------------------------------------------------

class basic_stream {
protected:
    atf_process_stream_t m_sb;
    bool m_inited;

    const atf_process_stream_t* get_sb(void) const;

public:
    basic_stream(void);
    ~basic_stream(void);
};

class stream_capture : basic_stream {
    // Allow access to the getters.
    template< class OutStream, class ErrStream > friend
    child fork(void (*)(void*), const OutStream&, const ErrStream&, void*);
    template< class OutStream, class ErrStream > friend
    status exec(const atf::fs::path&, const argv_array&,
                const OutStream&, const ErrStream&, void (*)(void));

public:
    stream_capture(void);
};

class stream_connect : basic_stream {
    // Allow access to the getters.
    template< class OutStream, class ErrStream > friend
    child fork(void (*)(void*), const OutStream&, const ErrStream&, void*);
    template< class OutStream, class ErrStream > friend
    status exec(const atf::fs::path&, const argv_array&,
                const OutStream&, const ErrStream&, void (*)(void));

public:
    stream_connect(const int, const int);
};

class stream_inherit : basic_stream {
    // Allow access to the getters.
    template< class OutStream, class ErrStream > friend
    child fork(void (*)(void*), const OutStream&, const ErrStream&, void*);
    template< class OutStream, class ErrStream > friend
    status exec(const atf::fs::path&, const argv_array&,
                const OutStream&, const ErrStream&, void (*)(void));

public:
    stream_inherit(void);
};

class stream_redirect_fd : basic_stream {
    // Allow access to the getters.
    template< class OutStream, class ErrStream > friend
    child fork(void (*)(void*), const OutStream&, const ErrStream&, void*);
    template< class OutStream, class ErrStream > friend
    status exec(const atf::fs::path&, const argv_array&,
                const OutStream&, const ErrStream&, void (*)(void));

public:
    stream_redirect_fd(const int);
};

class stream_redirect_path : basic_stream {
    // Allow access to the getters.
    template< class OutStream, class ErrStream > friend
    child fork(void (*)(void*), const OutStream&, const ErrStream&, void*);
    template< class OutStream, class ErrStream > friend
    status exec(const atf::fs::path&, const argv_array&,
                const OutStream&, const ErrStream&, void (*)(void));

public:
    stream_redirect_path(const fs::path&);
};

// ------------------------------------------------------------------------
// The "status" type.
// ------------------------------------------------------------------------

class status {
    atf_process_status_t m_status;

    friend class child;
    template< class OutStream, class ErrStream > friend
    status exec(const atf::fs::path&, const argv_array&,
                const OutStream&, const ErrStream&, void (*)(void));

    status(atf_process_status_t&);

public:
    ~status(void);

    bool exited(void) const;
    int exitstatus(void) const;

    bool signaled(void) const;
    int termsig(void) const;
    bool coredump(void) const;
};

// ------------------------------------------------------------------------
// The "child" type.
// ------------------------------------------------------------------------

class child {
    atf_process_child_t m_child;
    bool m_waited;

    template< class OutStream, class ErrStream > friend
    child fork(void (*)(void*), const OutStream&, const ErrStream&, void*);

    child(atf_process_child_t& c);

public:
    ~child(void);

    status wait(void);

    pid_t pid(void) const;
    int stdout_fd(void);
    int stderr_fd(void);
};

// ------------------------------------------------------------------------
// Free functions.
// ------------------------------------------------------------------------

namespace detail {
void flush_streams(void);
} // namespace detail

// TODO: The void* cookie can probably be templatized, thus also allowing
// const data structures.
template< class OutStream, class ErrStream >
child
fork(void (*start)(void*), const OutStream& outsb,
     const ErrStream& errsb, void* v)
{
    atf_process_child_t c;

    detail::flush_streams();
    atf_error_t err = atf_process_fork(&c, start, outsb.get_sb(),
                                       errsb.get_sb(), v);
    if (atf_is_error(err))
        throw_atf_error(err);

    return child(c);
}

template< class OutStream, class ErrStream >
status
exec(const atf::fs::path& prog, const argv_array& argv,
     const OutStream& outsb, const ErrStream& errsb,
     void (*prehook)(void))
{
    atf_process_status_t s;

    detail::flush_streams();
    atf_error_t err = atf_process_exec_array(&s, prog.c_path(),
                                             argv.exec_argv(),
                                             outsb.get_sb(),
                                             errsb.get_sb(),
                                             prehook);
    if (atf_is_error(err))
        throw_atf_error(err);

    return status(s);
}

template< class OutStream, class ErrStream >
status
exec(const atf::fs::path& prog, const argv_array& argv,
     const OutStream& outsb, const ErrStream& errsb)
{
    return exec(prog, argv, outsb, errsb, NULL);
}

} // namespace process
} // namespace atf

#endif // !defined(ATF_CXX_DETAIL_PROCESS_HPP)
