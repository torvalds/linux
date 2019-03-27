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

extern "C" {
#include <signal.h>

#include "atf-c/detail/process.h"
#include "atf-c/error.h"
}

#include <iostream>

#include "atf-c++/detail/exceptions.hpp"
#include "atf-c++/detail/sanity.hpp"

namespace detail = atf::process::detail;
namespace impl = atf::process;
#define IMPL_NAME "atf::process"

// ------------------------------------------------------------------------
// Auxiliary functions.
// ------------------------------------------------------------------------

template< class C >
atf::auto_array< const char* >
collection_to_argv(const C& c)
{
    atf::auto_array< const char* > argv(new const char*[c.size() + 1]);

    std::size_t pos = 0;
    for (typename C::const_iterator iter = c.begin(); iter != c.end();
         iter++) {
        argv[pos] = (*iter).c_str();
        pos++;
    }
    INV(pos == c.size());
    argv[pos] = NULL;

    return argv;
}

template< class C >
C
argv_to_collection(const char* const* argv)
{
    C c;

    for (const char* const* iter = argv; *iter != NULL; iter++)
        c.push_back(std::string(*iter));

    return c;
}

// ------------------------------------------------------------------------
// The "argv_array" type.
// ------------------------------------------------------------------------

impl::argv_array::argv_array(void) :
    m_exec_argv(collection_to_argv(m_args))
{
}

impl::argv_array::argv_array(const char* arg1, ...)
{
    m_args.push_back(arg1);

    {
        va_list ap;
        const char* nextarg;

        va_start(ap, arg1);
        while ((nextarg = va_arg(ap, const char*)) != NULL)
            m_args.push_back(nextarg);
        va_end(ap);
    }

    ctor_init_exec_argv();
}

impl::argv_array::argv_array(const char* const* ca) :
    m_args(argv_to_collection< args_vector >(ca)),
    m_exec_argv(collection_to_argv(m_args))
{
}

impl::argv_array::argv_array(const argv_array& a) :
    m_args(a.m_args),
    m_exec_argv(collection_to_argv(m_args))
{
}

void
impl::argv_array::ctor_init_exec_argv(void)
{
    m_exec_argv = collection_to_argv(m_args);
}

const char* const*
impl::argv_array::exec_argv(void)
    const
{
    return m_exec_argv.get();
}

impl::argv_array::size_type
impl::argv_array::size(void)
    const
{
    return m_args.size();
}

const char*
impl::argv_array::operator[](int idx)
    const
{
    return m_args[idx].c_str();
}

impl::argv_array::const_iterator
impl::argv_array::begin(void)
    const
{
    return m_args.begin();
}

impl::argv_array::const_iterator
impl::argv_array::end(void)
    const
{
    return m_args.end();
}

impl::argv_array&
impl::argv_array::operator=(const argv_array& a)
{
    if (this != &a) {
        m_args = a.m_args;
        m_exec_argv = collection_to_argv(m_args);
    }
    return *this;
}

// ------------------------------------------------------------------------
// The "stream" types.
// ------------------------------------------------------------------------

impl::basic_stream::basic_stream(void) :
    m_inited(false)
{
}

impl::basic_stream::~basic_stream(void)
{
    if (m_inited)
        atf_process_stream_fini(&m_sb);
}

const atf_process_stream_t*
impl::basic_stream::get_sb(void)
    const
{
    INV(m_inited);
    return &m_sb;
}

impl::stream_capture::stream_capture(void)
{
    atf_error_t err = atf_process_stream_init_capture(&m_sb);
    if (atf_is_error(err))
        throw_atf_error(err);
    m_inited = true;
}

impl::stream_connect::stream_connect(const int src_fd, const int tgt_fd)
{
    atf_error_t err = atf_process_stream_init_connect(&m_sb, src_fd, tgt_fd);
    if (atf_is_error(err))
        throw_atf_error(err);
    m_inited = true;
}

impl::stream_inherit::stream_inherit(void)
{
    atf_error_t err = atf_process_stream_init_inherit(&m_sb);
    if (atf_is_error(err))
        throw_atf_error(err);
    m_inited = true;
}

impl::stream_redirect_fd::stream_redirect_fd(const int fd)
{
    atf_error_t err = atf_process_stream_init_redirect_fd(&m_sb, fd);
    if (atf_is_error(err))
        throw_atf_error(err);
    m_inited = true;
}

impl::stream_redirect_path::stream_redirect_path(const fs::path& p)
{
    atf_error_t err = atf_process_stream_init_redirect_path(&m_sb, p.c_path());
    if (atf_is_error(err))
        throw_atf_error(err);
    m_inited = true;
}

// ------------------------------------------------------------------------
// The "status" type.
// ------------------------------------------------------------------------

impl::status::status(atf_process_status_t& s) :
    m_status(s)
{
}

impl::status::~status(void)
{
    atf_process_status_fini(&m_status);
}

bool
impl::status::exited(void)
    const
{
    return atf_process_status_exited(&m_status);
}

int
impl::status::exitstatus(void)
    const
{
    return atf_process_status_exitstatus(&m_status);
}

bool
impl::status::signaled(void)
    const
{
    return atf_process_status_signaled(&m_status);
}

int
impl::status::termsig(void)
    const
{
    return atf_process_status_termsig(&m_status);
}

bool
impl::status::coredump(void)
    const
{
    return atf_process_status_coredump(&m_status);
}

// ------------------------------------------------------------------------
// The "child" type.
// ------------------------------------------------------------------------

impl::child::child(atf_process_child_t& c) :
    m_child(c),
    m_waited(false)
{
}

impl::child::~child(void)
{
    if (!m_waited) {
        ::kill(atf_process_child_pid(&m_child), SIGTERM);

        atf_process_status_t s;
        atf_error_t err = atf_process_child_wait(&m_child, &s);
        INV(!atf_is_error(err));
        atf_process_status_fini(&s);
    }
}

impl::status
impl::child::wait(void)
{
    atf_process_status_t s;

    atf_error_t err = atf_process_child_wait(&m_child, &s);
    if (atf_is_error(err))
        throw_atf_error(err);

    m_waited = true;
    return status(s);
}

pid_t
impl::child::pid(void)
    const
{
    return atf_process_child_pid(&m_child);
}

int
impl::child::stdout_fd(void)
{
    return atf_process_child_stdout(&m_child);
}

int
impl::child::stderr_fd(void)
{
    return atf_process_child_stderr(&m_child);
}

// ------------------------------------------------------------------------
// Free functions.
// ------------------------------------------------------------------------

void
detail::flush_streams(void)
{
    // TODO: This should only be executed when inheriting the stdout or
    // stderr file descriptors.  However, the flushing is specific to the
    // iostreams, so we cannot do it from the C library where all the process
    // logic is performed.  Come up with a better design.
    std::cout.flush();
    std::cerr.flush();
}
