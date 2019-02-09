/*
 * Header file for multi buffer SHA256 algorithm data structure
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 *  Copyright(c) 2016 Intel Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  Contact Information:
 *      Megha Dey <megha.dey@linux.intel.com>
 *
 *  BSD LICENSE
 *
 *  Copyright(c) 2016 Intel Corporation.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

# Macros for defining data structures

# Usage example

#START_FIELDS   # JOB_AES
###     name            size    align
#FIELD  _plaintext,     8,      8       # pointer to plaintext
#FIELD  _ciphertext,    8,      8       # pointer to ciphertext
#FIELD  _IV,            16,     8       # IV
#FIELD  _keys,          8,      8       # pointer to keys
#FIELD  _len,           4,      4       # length in bytes
#FIELD  _status,        4,      4       # status enumeration
#FIELD  _user_data,     8,      8       # pointer to user data
#UNION  _union,         size1,  align1, \
#                       size2,  align2, \
#                       size3,  align3, \
#                       ...
#END_FIELDS
#%assign _JOB_AES_size  _FIELD_OFFSET
#%assign _JOB_AES_align _STRUCT_ALIGN

#########################################################################

# Alternate "struc-like" syntax:
#       STRUCT job_aes2
#       RES_Q   .plaintext,     1
#       RES_Q   .ciphertext,    1
#       RES_DQ  .IV,            1
#       RES_B   .nested,        _JOB_AES_SIZE, _JOB_AES_ALIGN
#       RES_U   .union,         size1, align1, \
#                               size2, align2, \
#                               ...
#       ENDSTRUCT
#       # Following only needed if nesting
#       %assign job_aes2_size   _FIELD_OFFSET
#       %assign job_aes2_align  _STRUCT_ALIGN
#
# RES_* macros take a name, a count and an optional alignment.
# The count in in terms of the base size of the macro, and the
# default alignment is the base size.
# The macros are:
# Macro    Base size
# RES_B     1
# RES_W     2
# RES_D     4
# RES_Q     8
# RES_DQ   16
# RES_Y    32
# RES_Z    64
#
# RES_U defines a union. It's arguments are a name and two or more
# pairs of "size, alignment"
#
# The two assigns are only needed if this structure is being nested
# within another. Even if the assigns are not done, one can still use
# STRUCT_NAME_size as the size of the structure.
#
# Note that for nesting, you still need to assign to STRUCT_NAME_size.
#
# The differences between this and using "struc" directly are that each
# type is implicitly aligned to its natural length (although this can be
# over-ridden with an explicit third parameter), and that the structure
# is padded at the end to its overall alignment.
#

#########################################################################

#ifndef _DATASTRUCT_ASM_
#define _DATASTRUCT_ASM_

#define PTR_SZ                  8
#define SHA512_DIGEST_WORD_SIZE 8
#define SHA512_MB_MGR_NUM_LANES_AVX2 4
#define NUM_SHA512_DIGEST_WORDS 8
#define SZ4                     4*SHA512_DIGEST_WORD_SIZE
#define ROUNDS                  80*SZ4
#define SHA512_DIGEST_ROW_SIZE  (SHA512_MB_MGR_NUM_LANES_AVX2 * 8)

# START_FIELDS
.macro START_FIELDS
 _FIELD_OFFSET = 0
 _STRUCT_ALIGN = 0
.endm

# FIELD name size align
.macro FIELD name size align
 _FIELD_OFFSET = (_FIELD_OFFSET + (\align) - 1) & (~ ((\align)-1))
 \name  = _FIELD_OFFSET
 _FIELD_OFFSET = _FIELD_OFFSET + (\size)
.if (\align > _STRUCT_ALIGN)
 _STRUCT_ALIGN = \align
.endif
.endm

# END_FIELDS
.macro END_FIELDS
 _FIELD_OFFSET = (_FIELD_OFFSET + _STRUCT_ALIGN-1) & (~ (_STRUCT_ALIGN-1))
.endm

.macro STRUCT p1
START_FIELDS
.struc \p1
.endm

.macro ENDSTRUCT
 tmp = _FIELD_OFFSET
 END_FIELDS
 tmp = (_FIELD_OFFSET - ##tmp)
.if (tmp > 0)
        .lcomm  tmp
.endm

## RES_int name size align
.macro RES_int p1 p2 p3
 name = \p1
 size = \p2
 align = .\p3

 _FIELD_OFFSET = (_FIELD_OFFSET + (align) - 1) & (~ ((align)-1))
.align align
.lcomm name size
 _FIELD_OFFSET = _FIELD_OFFSET + (size)
.if (align > _STRUCT_ALIGN)
 _STRUCT_ALIGN = align
.endif
.endm

# macro RES_B name, size [, align]
.macro RES_B _name, _size, _align=1
RES_int _name _size _align
.endm

# macro RES_W name, size [, align]
.macro RES_W _name, _size, _align=2
RES_int _name 2*(_size) _align
.endm

# macro RES_D name, size [, align]
.macro RES_D _name, _size, _align=4
RES_int _name 4*(_size) _align
.endm

# macro RES_Q name, size [, align]
.macro RES_Q _name, _size, _align=8
RES_int _name 8*(_size) _align
.endm

# macro RES_DQ name, size [, align]
.macro RES_DQ _name, _size, _align=16
RES_int _name 16*(_size) _align
.endm

# macro RES_Y name, size [, align]
.macro RES_Y _name, _size, _align=32
RES_int _name 32*(_size) _align
.endm

# macro RES_Z name, size [, align]
.macro RES_Z _name, _size, _align=64
RES_int _name 64*(_size) _align
.endm

#endif

###################################################################
### Define SHA512 Out Of Order Data Structures
###################################################################

START_FIELDS    # LANE_DATA
###     name            size    align
FIELD   _job_in_lane,   8,      8       # pointer to job object
END_FIELDS

 _LANE_DATA_size = _FIELD_OFFSET
 _LANE_DATA_align = _STRUCT_ALIGN

####################################################################

START_FIELDS    # SHA512_ARGS_X4
###     name            size    align
FIELD   _digest,        8*8*4,  4      # transposed digest
FIELD   _data_ptr,      8*4,    8       # array of pointers to data
END_FIELDS

 _SHA512_ARGS_X4_size  =  _FIELD_OFFSET
 _SHA512_ARGS_X4_align =  _STRUCT_ALIGN

#####################################################################

START_FIELDS    # MB_MGR
###     name            size    align
FIELD   _args,          _SHA512_ARGS_X4_size, _SHA512_ARGS_X4_align
FIELD   _lens,          8*4,    8
FIELD   _unused_lanes,  8,      8
FIELD   _ldata,         _LANE_DATA_size*4, _LANE_DATA_align
END_FIELDS

 _MB_MGR_size  =  _FIELD_OFFSET
 _MB_MGR_align =  _STRUCT_ALIGN

_args_digest = _args + _digest
_args_data_ptr = _args + _data_ptr

#######################################################################

#######################################################################
#### Define constants
#######################################################################

#define STS_UNKNOWN             0
#define STS_BEING_PROCESSED     1
#define STS_COMPLETED           2

#######################################################################
#### Define JOB_SHA512 structure
#######################################################################

START_FIELDS    # JOB_SHA512
###     name                            size    align
FIELD   _buffer,                        8,      8       # pointer to buffer
FIELD   _len,                           8,      8       # length in bytes
FIELD   _result_digest,                 8*8,    32      # Digest (output)
FIELD   _status,                        4,      4
FIELD   _user_data,                     8,      8
END_FIELDS

 _JOB_SHA512_size = _FIELD_OFFSET
 _JOB_SHA512_align = _STRUCT_ALIGN
