/*
 * Userspace interface to the SDIO Userspace Interface driver.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef LINUX_SDIOEMB_UIF_H
#define LINUX_SDIOEMB_UIF_H

enum sdioemb_uif_cmd_type {
    SDIOEMB_UIF_CMD52_READ, SDIOEMB_UIF_CMD52_WRITE,
    SDIOEMB_UIF_CMD53_READ, SDIOEMB_UIF_CMD53_WRITE,
};

struct sdioemb_uif_cmd {
    enum sdioemb_uif_cmd_type type;
    int                    function;
    uint32_t               address;
    uint8_t *              data;
    size_t                 len;
    int                    block_size;
};

#define SDIOEMB_UIF_IOC_MAGIC 's'

#define SDIOEMB_UIF_IOCQNUMFUNCS  _IO(SDIOEMB_UIF_IOC_MAGIC,   0)
#define SDIOEMB_UIF_IOCCMD        _IOWR(SDIOEMB_UIF_IOC_MAGIC, 1, struct sdioemb_uif_cmd)
#define SDIOEMB_UIF_IOCWAITFORINT _IO(SDIOEMB_UIF_IOC_MAGIC,   2)
#define SDIOEMB_UIF_IOCTBUSWIDTH  _IO(SDIOEMB_UIF_IOC_MAGIC,   3)
#define SDIOEMB_UIF_IOCREINSERT   _IO(SDIOEMB_UIF_IOC_MAGIC,   4)
#define SDIOEMB_UIF_IOCTBUSFREQ   _IO(SDIOEMB_UIF_IOC_MAGIC,   5)
#define SDIOEMB_UIF_IOCQMANFID    _IO(SDIOEMB_UIF_IOC_MAGIC,   6)
#define SDIOEMB_UIF_IOCQCARDID    _IO(SDIOEMB_UIF_IOC_MAGIC,   7)
#define SDIOEMB_UIF_IOCQSTDIF     _IO(SDIOEMB_UIF_IOC_MAGIC,   8)
#define SDIOEMB_UIF_IOCQMAXBLKSZ  _IO(SDIOEMB_UIF_IOC_MAGIC,   9)
#define SDIOEMB_UIF_IOCQBLKSZ     _IO(SDIOEMB_UIF_IOC_MAGIC,  10)
#define SDIOEMB_UIF_IOCTBLKSZ     _IO(SDIOEMB_UIF_IOC_MAGIC,  11)

#endif /* #ifndef LINUX_SDIOEMB_UIF_H */
