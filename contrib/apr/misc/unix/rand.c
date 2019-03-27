/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define APR_WANT_MEMFUNC
#include "apr_want.h"
#include "apr_general.h"

#include "apr_arch_misc.h"
#include <sys/stat.h>
#if APR_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if APR_HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if APR_HAVE_FCNTL_H
#include <fcntl.h>
#endif
#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif
#if APR_HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#if defined(HAVE_UUID_H)
#include <uuid.h>
#elif defined(HAVE_UUID_UUID_H)
#include <uuid/uuid.h>
#elif defined(HAVE_SYS_UUID_H)
#include <sys/uuid.h>
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

#if APR_HAS_OS_UUID

#if defined(HAVE_UUID_CREATE)

APR_DECLARE(apr_status_t) apr_os_uuid_get(unsigned char *uuid_data)
{
    uint32_t rv;
    uuid_t g;

    uuid_create(&g, &rv);

    if (rv != uuid_s_ok)
        return APR_EGENERAL;

    memcpy(uuid_data, &g, sizeof(uuid_t));

    return APR_SUCCESS;
}

#elif defined(HAVE_UUID_GENERATE)

APR_DECLARE(apr_status_t) apr_os_uuid_get(unsigned char *uuid_data)
{
    uuid_t g;

    uuid_generate(g);

    memcpy(uuid_data, g, sizeof(uuid_t));

    return APR_SUCCESS;
}
#endif 

#endif /* APR_HAS_OS_UUID */

#if APR_HAS_RANDOM

APR_DECLARE(apr_status_t) apr_generate_random_bytes(unsigned char *buf, 
                                                    apr_size_t length)
{
#ifdef DEV_RANDOM

    int fd = -1;

    /* On BSD/OS 4.1, /dev/random gives out 8 bytes at a time, then
     * gives EOF, so reading 'length' bytes may require opening the
     * device several times. */
    do {
        apr_ssize_t rc;

        if (fd == -1)
            if ((fd = open(DEV_RANDOM, O_RDONLY)) == -1)
                return errno;
        
        do {
            rc = read(fd, buf, length);
        } while (rc == -1 && errno == EINTR);

        if (rc < 0) {
            int errnum = errno;
            close(fd);
            return errnum;
        }
        else if (rc == 0) {
            close(fd);
            fd = -1; /* force open() again */
        }
        else {
            buf += rc;
            length -= rc;
        }
    } while (length > 0);
    
    close(fd);
#elif defined(OS2)
    static UCHAR randbyte();
    unsigned int idx;

    for (idx=0; idx<length; idx++)
	buf[idx] = randbyte();

#elif defined(HAVE_EGD)
    /* use EGD-compatible socket daemon (such as EGD or PRNGd).
     * message format:
     * 0x00 (get entropy level)
     *   0xMM (msb) 0xmm 0xll 0xLL (lsb)
     * 0x01 (read entropy nonblocking) 0xNN (bytes requested)
     *   0xMM (bytes granted) MM bytes
     * 0x02 (read entropy blocking) 0xNN (bytes desired)
     *   [block] NN bytes
     * 0x03 (write entropy) 0xMM 0xLL (bits of entropy) 0xNN (bytes of data) 
     *      NN bytes
     * (no response - write only) 
     * 0x04 (report PID)
     *   0xMM (length of PID string, not null-terminated) MM chars
     */
    static const char *egd_sockets[] = { EGD_DEFAULT_SOCKET, NULL };
    const char **egdsockname = NULL;

    int egd_socket, egd_path_len, rv, bad_errno;
    struct sockaddr_un addr;
    apr_socklen_t egd_addr_len;
    apr_size_t resp_expected;
    unsigned char req[2], resp[255];
    unsigned char *curbuf = buf;

    for (egdsockname = egd_sockets; *egdsockname && length > 0; egdsockname++) {
        egd_path_len = strlen(*egdsockname);
        
        if (egd_path_len > sizeof(addr.sun_path)) {
            return APR_EINVAL;
        }

        memset(&addr, 0, sizeof(struct sockaddr_un));
        addr.sun_family = AF_UNIX;
        memcpy(addr.sun_path, *egdsockname, egd_path_len);
        egd_addr_len = APR_OFFSETOF(struct sockaddr_un, sun_path) + 
          egd_path_len; 

        egd_socket = socket(PF_UNIX, SOCK_STREAM, 0);

        if (egd_socket == -1) {
            return errno;
        }

        rv = connect(egd_socket, (struct sockaddr*)&addr, egd_addr_len);

        if (rv == -1) {
            bad_errno = errno;
            continue;
        }

        /* EGD can only return 255 bytes of data at a time.  Silly.  */ 
        while (length > 0) {
            apr_ssize_t srv;
            req[0] = 2; /* We'll block for now. */
            req[1] = length > 255 ? 255: length;

            srv = write(egd_socket, req, 2);
            if (srv == -1) {
                bad_errno = errno;
                shutdown(egd_socket, SHUT_RDWR);
                close(egd_socket);
                break;
            }

            if (srv != 2) {
                shutdown(egd_socket, SHUT_RDWR);
                close(egd_socket);
                return APR_EGENERAL;
            }
            
            resp_expected = req[1];
            srv = read(egd_socket, resp, resp_expected);
            if (srv == -1) {
                bad_errno = errno;
                shutdown(egd_socket, SHUT_RDWR);
                close(egd_socket);
                return bad_errno;
            }
            
            memcpy(curbuf, resp, srv);
            curbuf += srv;
            length -= srv;
        }
        
        shutdown(egd_socket, SHUT_RDWR);
        close(egd_socket);
    }

    if (length > 0) {
        /* We must have iterated through the list of sockets,
         * and no go. Return the errno.
         */
        return bad_errno;
    }

#elif defined(HAVE_TRUERAND) /* use truerand */

    extern int randbyte(void);	/* from the truerand library */
    unsigned int idx;

    /* this will increase the startup time of the server, unfortunately...
     * (generating 20 bytes takes about 8 seconds)
     */
    for (idx=0; idx<length; idx++)
	buf[idx] = (unsigned char) randbyte();

#endif	/* DEV_RANDOM */

    return APR_SUCCESS;
}

#undef	STR
#undef	XSTR

#ifdef OS2
#include "randbyte_os2.inc"
#endif

#endif /* APR_HAS_RANDOM */
