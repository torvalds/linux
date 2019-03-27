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

#if !defined(ATF_CXX_DETAIL_ENV_HPP)
#define ATF_CXX_DETAIL_ENV_HPP

#include <string>

namespace atf {
namespace env {

// ------------------------------------------------------------------------
// Free functions.
// ------------------------------------------------------------------------

//!
//! \brief Returns the value of an environment variable.
//!
//! Returns the value of the specified environment variable.  The variable
//! must be defined.
//!
std::string get(const std::string&);

//!
//! \brief Returns the value of an environment variable with a default.
//!
std::string get(const std::string&, const std::string&);

//!
//! \brief Checks if the environment has a variable.
//!
//! Checks if the environment has a given variable.
//!
bool has(const std::string&);

//!
//! \brief Sets an environment variable to a given value.
//!
//! Sets the specified environment variable to the given value.  Note that
//! variables set to the empty string are different to undefined ones.
//!
//! Be aware that this alters the program's global status, which in general
//! is a bad thing to do due to the side-effects it may have.  There are
//! some legitimate usages for this function, though.
//!
void set(const std::string&, const std::string&);

//!
//! \brief Unsets an environment variable.
//!
//! Unsets the specified environment variable  Note that undefined
//! variables are different to those defined but set to an empty value.
//!
//! Be aware that this alters the program's global status, which in general
//! is a bad thing to do due to the side-effects it may have.  There are
//! some legitimate usages for this function, though.
//!
void unset(const std::string&);

} // namespace env
} // namespace atf

#endif // !defined(ATF_CXX_DETAIL_ENV_HPP)
