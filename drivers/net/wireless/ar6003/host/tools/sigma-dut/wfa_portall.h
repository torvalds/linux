#ifndef _WFA_PORTALL_H_
#define _WFA_PORTALL_H_

/* The fixed definition for the device interface name */
/* For different OSes or your platforms, you may not  */ 
/* need to use fixed definition                       */
#define WFA_STAUT_IF        "wlan0"

/* the path to store EAP certificates. This is used   */
/* for Linux-like OS for wpa_supplicant               */
#define CERTIFICATES_PATH    "/etc/wpa_supplicant"


#define wSTRNCPY(dstr, sstr, len) \
                           strncpy(dstr, sstr, len)

#define wSTRCPY(dstr, sstr) \
                           strcpy(dstr, sstr)

#define wSTRNCPY(dstr, sstr, len) \
                           strncpy(dstr, sstr, len)

#define wSTRNCMP(str1, str2, size) \
                           strncmp(str1, str2, size)

#define wSTRLEN(str)     \
                           strlen(str)

#define wSOCKET(family, type, protocol) \
                           socket(family, type, protocol)

#define wSELECT(maxfdn, rsockSet, wsockSet, exceptSet, timer)  \
                           select(maxfdn, rsockSet, wsockSet, exceptSet, timer)

#define wSETSOCKOPT(sock, level, optname, optval, optlen) \
                           setsockopt(sock, level, optname, optval, optlen)

#define wBIND(sock, sockaddr, sizeaddr) \
                           bind(sock, sockaddr, sizeaddr)

#define wLISTEN(sock, maxnumpending)  \
                           listen(sock, maxnumpending)

#define wBZERO(ap, memsize) \
                           bzero(ap, memsize)

#define wCONNECT(sock, peeraddr, sizeaddr) \
                           connect(sock, peeraddr, sizeaddr)

#define wACCEPT(sock, addr, sizeaddr) \
                           accept(sock, addr, sizeaddr)

#define wSEND(sock, buf, len, flag) \
                           send(sock, buf, len, flag)

#define wSENDTO(sock, buf, len, flag, toaddr, sizeaddr) \
                           sendto(sock, buf, len, flag, toaddr, sizeaddr)

#define wRECV(sock, buf, len, flag) \
                           recv(sock, buf, len, flag)

#define wRECVFROM(sock, buf, len, flag, fromaddr, sizeaddr) \
                           recvfrom(sock, buf, len, flag, fromaddr, sizeaddr)


#define wFCNTL(fd, cmd, args) \
                           fcntl(fd, cmd, args)

#define wSYSTEM(cmd) \
			   system(cmd)

#define wSPRINTF(buf, format...) \
			   sprintf(buf, ## format)

#define wIOCTL(fd, cmd, intf) \
                           ioctl(fd, cmd, intf)

#define wMALLOC(size)  \
                           malloc(size)

#define wMEMCPY(dp, sp, size) \
                           memcpy(dp, sp, size)

#define wMEMSET(memp, val, size)  \
                           memset(memp, val, size)

#define wFREE(memp)      \
                           free(memp); 

#define wGETTIMEOFDAY(tmval, zone) \
                           gettimeofday(tmval, zone) 

#define wSETTIMEOFDAY(tmval, zone) \
                           settimeofday(tmval, zone) 

#define wSIGNAL(signum, sighandler) \
                           signal(signum, sighandler)

#define wALARM(duration) \
                           alarm(duration)

#define wCLOSE(fd)       \
                           close(fd);

#define wUSLEEP(usecs)   \
                           usleep(usecs)


#define wSLEEP(secs)   \
                           sleep(secs)

#define wEXIT(num)     \
                           exit(num)

#define wATOI(c)       \
                           atoi(c)

#define wSHUTDOWN(fd, type)      \
                           shutdown(fd, type)

#define wGETSOFD(sockfd, proto, tosval, ptosval, psize) \
                           getsockopt(sockfd, proto, tosval, ptosval, psize)

#define wPT_ATTR_INIT(ptattr) \
                           pthread_attr_init(ptattr)

#define wPT_ATTR_SETSCH(ptattr, policy) \
                           pthread_attr_setschedpolicy(ptattr, policy)   

#define wPT_ATTR_SETSCHPARAM(ptattr, param) \
                           pthread_attr_setschedparam(ptattr, param)

#define wPT_MUTEX_LOCK(lock) \
                           pthread_mutex_lock(lock)

#define wPT_MUTEX_UNLOCK(lock) \
                           pthread_mutex_unlock(lock)

#define wFFLUSH(out) \
                           fflush(out)

#define wPT_COND_SIGNAL(signal) \
                           pthread_cond_signal(signal)

#define wPT_COND_WAIT(pcond, pmlock) \
                           pthread_cond_wait(pcond, pmlock)

#define wPT_MUTEX_INIT(pmlock, initval) \
                           pthread_mutex_init(pmlock, initval)

#define wPT_COND_INIT(pcond, initval) \
                           pthread_cond_init(pcond, initval)

#define wPT_CREATE(t, ptattr, func, pdata) \
                           pthread_create(t, ptattr, func, pdata)


typedef struct _memblock
{
    int id;
    int size;    /* number of blocks 512 byte/block */
    void *mem;
} wfaMemBlk_t;

#endif
