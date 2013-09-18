 /*
  * PMC-Sierra SPC 8001 SAS/SATA based host adapters driver
  *
  * Copyright (c) 2008-2009 USI Co., Ltd.
  * All rights reserved.
  *
  * Redistribution and use in source and binary forms, with or without
  * modification, are permitted provided that the following conditions
  * are met:
  * 1. Redistributions of source code must retain the above copyright
  *    notice, this list of conditions, and the following disclaimer,
  *    without modification.
  * 2. Redistributions in binary form must reproduce at minimum a disclaimer
  *    substantially similar to the "NO WARRANTY" disclaimer below
  *    ("Disclaimer") and any redistribution must be conditioned upon
  *    including a substantially similar Disclaimer requirement for further
  *    binary redistribution.
  * 3. Neither the names of the above-listed copyright holders nor the names
  *    of any contributors may be used to endorse or promote products derived
  *    from this software without specific prior written permission.
  *
  * Alternatively, this software may be distributed under the terms of the
  * GNU General Public License ("GPL") version 2 as published by the Free
  * Software Foundation.
  *
  * NO WARRANTY
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
  * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
  * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
  * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
  * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
  * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  * POSSIBILITY OF SUCH DAMAGES.
  *
  */

#ifndef PM8001_CTL_H_INCLUDED
#define PM8001_CTL_H_INCLUDED

#define IOCTL_BUF_SIZE		4096
#define HEADER_LEN			28
#define SIZE_OFFSET			16

#define BIOSOFFSET			56
#define BIOS_OFFSET_LIMIT		61

#define FLASH_OK                        0x000000
#define FAIL_OPEN_BIOS_FILE             0x000100
#define FAIL_FILE_SIZE                  0x000a00
#define FAIL_PARAMETERS                 0x000b00
#define FAIL_OUT_MEMORY                 0x000c00
#define FLASH_IN_PROGRESS               0x001000

#endif /* PM8001_CTL_H_INCLUDED */

