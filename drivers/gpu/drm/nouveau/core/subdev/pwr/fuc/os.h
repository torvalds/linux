#ifndef __NVKM_PWR_OS_H__
#define __NVKM_PWR_OS_H__

/* Process names */
#define PROC_KERN 0x52544e49
#define PROC_IDLE 0x454c4449
#define PROC_HOST 0x54534f48
#define PROC_MEMX 0x584d454d
#define PROC_PERF 0x46524550
#define PROC_TEST 0x54534554

/* KERN: message identifiers */
#define KMSG_FIFO   0x00000000
#define KMSG_ALARM  0x00000001

/* MEMX: message identifiers */
#define MEMX_MSG_INFO 0
#define MEMX_MSG_EXEC 1

/* MEMX: script opcode definitions */
#define MEMX_ENTER  0
#define MEMX_LEAVE  1
#define MEMX_WR32   2
#define MEMX_WAIT   3
#define MEMX_DELAY  4

#endif
