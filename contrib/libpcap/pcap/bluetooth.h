/*
 * Copyright (c) 2006 Paolo Abeni (Italy)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * bluetooth data struct
 * By Paolo Abeni <paolo.abeni@email.it>
 */

#ifndef lib_pcap_bluetooth_h
#define lib_pcap_bluetooth_h

#include <pcap/pcap-inttypes.h>

/*
 * Header prepended libpcap to each bluetooth h4 frame,
 * fields are in network byte order
 */
typedef struct _pcap_bluetooth_h4_header {
	uint32_t direction; /* if first bit is set direction is incoming */
} pcap_bluetooth_h4_header;

/*
 * Header prepended libpcap to each bluetooth linux monitor frame,
 * fields are in network byte order
 */
typedef struct _pcap_bluetooth_linux_monitor_header {
	uint16_t adapter_id;
	uint16_t opcode;
} pcap_bluetooth_linux_monitor_header;

#endif
