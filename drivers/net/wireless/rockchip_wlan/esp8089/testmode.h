
#ifndef __TEST_MODE
#define __TEST_MODE

enum {
        TEST_CMD_UNSPEC,
        TEST_CMD_ECHO,
        TEST_CMD_ASK,
        TEST_CMD_SLEEP,
        TEST_CMD_WAKEUP,
        TEST_CMD_LOOPBACK,
        TEST_CMD_TX,
        TEST_CMD_RX,
        TEST_CMD_DEBUG,
        TEST_CMD_SDIO_WR,
        TEST_CMD_SDIO_RD,
        TEST_CMD_SDIOSPEED,
        __TEST_CMD_MAX,
};
#define TEST_CMD_MAX (__TEST_CMD_MAX - 1)

enum {
        TEST_ATTR_UNSPEC,
        TEST_ATTR_CMD_NAME,
        TEST_ATTR_CMD_TYPE,
        TEST_ATTR_PARA_NUM,
        TEST_ATTR_PARA0,
        TEST_ATTR_PARA1,
        TEST_ATTR_PARA2,
        TEST_ATTR_PARA3,
        TEST_ATTR_PARA4,
        TEST_ATTR_PARA5,
        TEST_ATTR_PARA6,
        TEST_ATTR_PARA7,
        TEST_ATTR_STR,
        __TEST_ATTR_MAX,
};
#define TEST_ATTR_MAX (__TEST_ATTR_MAX - 1)
#define TEST_ATTR_PARA(i) (TEST_ATTR_PARA0+(i))

u32 get_loopback_num(void);
u32 get_loopback_id(void);
void inc_loopback_id(void);
#endif


