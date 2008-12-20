/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  This file defines the EPL version for the stack, as string
                and for object 0x1018 within object dictionary.

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of SYSTEC electronic GmbH nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

  -------------------------------------------------------------------------

                $RCSfile: EplVersion.h,v $

                $Author: D.Krueger $

                $Revision: 1.6 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    all

  -------------------------------------------------------------------------

  Revision History:

****************************************************************************/

#ifndef _EPL_VERSION_H_
#define _EPL_VERSION_H_

// NOTE:
// All version macros should contain the same version number. But do not use
// defines instead of the numbers. Because the macro EPL_STRING_VERSION() can not
// convert a define to a string.
//
// Format: maj.min.build
//         maj            = major version
//             min        = minor version (will be set to 0 if major version will be incremented)
//                 build  = current build (will be set to 0 if minor version will be incremented)
//
#define DEFINED_STACK_VERSION       EPL_STACK_VERSION   (1, 3, 0)
#define DEFINED_OBJ1018_VERSION     EPL_OBJ1018_VERSION (1, 3, 0)
#define DEFINED_STRING_VERSION      EPL_STRING_VERSION  (1, 3, 0)

// -----------------------------------------------------------------------------
#define EPL_PRODUCT_NAME            "EPL V2"
#define EPL_PRODUCT_VERSION         DEFINED_STRING_VERSION
#define EPL_PRODUCT_MANUFACTURER    "SYS TEC electronic GmbH"

#define EPL_PRODUCT_KEY         "SO-1083"
#define EPL_PRODUCT_DESCRIPTION "openPOWERLINK Protocol Stack Source"

#endif // _EPL_VERSION_H_

// Die letzte Zeile muﬂ unbedingt eine leere Zeile sein, weil manche Compiler
// damit ein Problem haben, wenn das nicht so ist (z.B. GNU oder Borland C++ Builder).
