/*
 * \file       ocsd_if_version.h
 * \brief      OpenCSD : Library API versioning
 * 
 * \copyright  Copyright (c) 2016, ARM Limited. All Rights Reserved.
 */

/* 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors 
 * may be used to endorse or promote products derived from this software without 
 * specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 'AS IS' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */ 

#ifndef ARM_OCSD_IF_VERSION_H_INCLUDED
#define ARM_OCSD_IF_VERSION_H_INCLUDED

#include <stdint.h>

/** @addtogroup ocsd_interfaces
@{*/

/** @name Library Versioning
@{*/
#define OCSD_VER_MAJOR 0x0 /**< Library Major Version */
#define OCSD_VER_MINOR 0x8 /**< Library Minor Version */
#define OCSD_VER_PATCH 0x2 /**< Library Patch Version */

/** Library version number - MMMMnnpp format.
    MMMM = major version, 
    nn = minor version, 
    pp = patch version
*/
#define OCSD_VER_NUM (((uint32_t)OCSD_VER_MAJOR << 16) | ((uint32_t)OCSD_VER_MINOR << 8) | ((uint32_t)OCSD_VER_PATCH)) 

#define OCSD_VER_STRING "0.8.2"    /**< Library Version string */
#define OCSD_LIB_NAME "OpenCSD Library"  /**< Library name string */
#define OCSD_LIB_SHORT_NAME "OCSD"    /**< Library Short name string */
/** @}*/

/** @}*/

#endif // ARM_OCSD_IF_VERSION_H_INCLUDED

/* End of File ocsd_if_version.h */
