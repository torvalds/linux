#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <asm/delay.h>
#include <mach/am_regs.h>
#include <mach/power_gate.h>
#include <linux/amlogic/tvin/tvin.h>

#include <mach/gpio.h>
#include <linux/amlogic/hdmi_tx/hdmi_info_global.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_module.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_cec.h>
#include <mach/hdmi_tx_reg.h>

static DEFINE_MUTEX(cec_mutex);
static int cec_repeat_flag = 0;
static int repeat;
extern int cec_msg_dbg_en;
static hdmitx_dev_t* hdmitx_device = NULL;

/*************************** cec arbitration cts code ******************************/
// using the cec pin as fiq gpi to assist the bus arbitration

struct _cec_msg_ {
    unsigned char msg[16];
    unsigned char len;
};

static struct _cec_msg_ cec_msg_bak;

static unsigned long frame_time_log[512] = { 0 };
static unsigned long frame_time_idx = 0;
static int ack_check_point[16] = { 0 };
static unsigned char msg_log_buf[128] = { 0 };
static unsigned int cec_tx_start = 0;
static unsigned int cec_rx_start = 0;

#define BUS_LEVEL()         (!!(aml_read_reg32(P_PREG_PAD_GPIO2_I) & (1<<13)))

void cec_disable_irq(void)
{
    //Disable irq
    aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK, aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK) & ~(1 << 23));
    //Clear the interrupt
    aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_STAT_CLR, aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_STAT_CLR) | (1 << 23));
    hdmi_print(INF, CEC "disable:int mask:0x%x\n", aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK));
}
void cec_enable_irq(void)
{
    aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_STAT_CLR, aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_STAT_CLR) & ~(1 << 23));
    aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK, aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK) | (1 << 23));

    hdmi_print(INF, CEC "enable:int mask:0x%x\n", aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK));
}

void cec_hw_reset(void)
{
    aml_write_reg32(P_HDMI_CTRL_PORT, aml_read_reg32(P_HDMI_CTRL_PORT)|(1<<16));
    hdmi_wr_reg(OTHER_BASE_ADDR+HDMI_OTHER_CTRL0, 0xc); //[3]cec_creg_sw_rst [2]cec_sys_sw_rst
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_CLEAR_BUF, 0x1);
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_RX_CLEAR_BUF, 0x1);
    
    //mdelay(10);
    {//Delay some time
        int i = 10;
        while(i--);
    }
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_CLEAR_BUF, 0x0);
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_RX_CLEAR_BUF, 0x0);
    hdmi_wr_reg(OTHER_BASE_ADDR+HDMI_OTHER_CTRL0, 0x0);
    aml_write_reg32(P_HDMI_CTRL_PORT, aml_read_reg32(P_HDMI_CTRL_PORT)&(~(1<<16)));
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_CLOCK_DIV_H, 0x00 );
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_CLOCK_DIV_L, 0xf0 );

    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_LOGICAL_ADDR0, (0x1 << 4) | cec_global_info.my_node_index);

    hdmi_print(INF, CEC "hw reset :logical addr:0x%x\n", hdmi_rd_reg(CEC0_BASE_ADDR+CEC_LOGICAL_ADDR0));
}


int cec_ll_rx( unsigned char *msg, unsigned char *len)
{
    unsigned char i;
    unsigned char rx_status;
    unsigned char data;
    int pos;
    unsigned char n;
    unsigned char *msg_start = msg;
    int rx_msg_length;

    if(RX_DONE != hdmi_rd_reg(CEC0_BASE_ADDR+CEC_RX_MSG_STATUS)){
        hdmi_wr_reg(CEC0_BASE_ADDR + CEC_RX_MSG_CMD,  RX_ACK_CURRENT);
        hdmi_wr_reg(CEC0_BASE_ADDR + CEC_RX_MSG_CMD,  RX_NO_OP);
        return -1;
    }

    if(1 != hdmi_rd_reg(CEC0_BASE_ADDR+CEC_RX_NUM_MSG)){
        hdmi_wr_reg(CEC0_BASE_ADDR + CEC_RX_MSG_CMD,  RX_ACK_CURRENT);
        hdmi_wr_reg(CEC0_BASE_ADDR + CEC_RX_MSG_CMD,  RX_NO_OP);
        return -1;
    }
    
    rx_msg_length = hdmi_rd_reg(CEC0_BASE_ADDR + CEC_RX_MSG_LENGTH) + 1;

    hdmi_wr_reg(CEC0_BASE_ADDR + CEC_RX_MSG_CMD,  RX_ACK_CURRENT);

    for (i = 0; i < rx_msg_length && i < MAX_MSG; i++) {
        data = hdmi_rd_reg(CEC0_BASE_ADDR + CEC_RX_MSG_0_HEADER +i);
        *msg = data;
        msg++;
    }
    *len = rx_msg_length;
    rx_status = hdmi_rd_reg(CEC0_BASE_ADDR+CEC_RX_MSG_STATUS);

    hdmi_wr_reg(CEC0_BASE_ADDR + CEC_RX_MSG_CMD,  RX_NO_OP);

    if(cec_msg_dbg_en  == 1){
        pos = 0;
        pos += sprintf(msg_log_buf + pos, "CEC: rx msg len: %d   dat: ", rx_msg_length);
        for(n = 0; n < rx_msg_length; n++) {
            pos += sprintf(msg_log_buf + pos, "%02x ", msg_start[n]);
        }
        pos += sprintf(msg_log_buf + pos, "\n");
        msg_log_buf[pos] = '\0';
        hdmi_print(INF, CEC "%s", msg_log_buf);
    }
    return rx_status;
}

int cec_ll_tx_polling(const unsigned char *msg, unsigned char len)
{
    int i;
    unsigned int ret = 0xf;
    unsigned int n;
//	unsigned int j = 30;
    int pos;

    for (i = 0; i < len; i++) {
     hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_0_HEADER + i, msg[i]);
    }
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_LENGTH, len-1);
    //cec_tx_start = 1;
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_CMD, RX_ACK_CURRENT);//TX_REQ_NEXT
    msleep(len * 24 + 5);

    ret = hdmi_rd_reg(CEC0_BASE_ADDR+CEC_TX_MSG_STATUS);

    if(ret == TX_DONE)
        ret = 1;
    else
        ret = 0;

    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_CMD, TX_NO_OP);

    if(cec_msg_dbg_en  == 1) {
        pos = 0;
        pos += sprintf(msg_log_buf + pos, "CEC: tx msg len: %d   dat: ", len);
        for(n = 0; n < len; n++) {
            pos += sprintf(msg_log_buf + pos, "%02x ", msg[n]);
        }
        pos += sprintf(msg_log_buf + pos, "\nCEC: tx state: %d\n", ret);
        msg_log_buf[pos] = '\0';
        printk("%s", msg_log_buf);
    }
    return ret;
}


void cec_polling_online_dev(int log_addr, int *bool)
{
    unsigned long r;
    unsigned char msg[1];

    cec_global_info.my_node_index = log_addr;
    msg[0] = (log_addr<<4) | log_addr;

    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_LOGICAL_ADDR0, (0x1 << 4) | 0xf);
    r = cec_ll_tx(msg, 1);

    if (r == 0) {
        *bool = 0;
    }else{
        memset(&(cec_global_info.cec_node_info[log_addr]), 0, sizeof(cec_node_info_t));
        cec_global_info.cec_node_info[log_addr].dev_type = cec_log_addr_to_dev_type(log_addr);
    	  *bool = 1;
    }
    if(*bool == 0) {
        hdmi_wr_reg(CEC0_BASE_ADDR+CEC_LOGICAL_ADDR0, (0x1 << 4) | log_addr);
    }
    hdmi_print(INF, CEC "CEC: poll online logic device: 0x%x BOOL: %d\n", log_addr, *bool);

}

static void get_bus_free(void)
{
    unsigned int cnt = 0;
    do {
        frame_time_idx = 0;
        while(!BUS_LEVEL()) {  // judge whether cec bus level is low
            //msleep(17);
            cec_repeat_flag ? msleep(17) : ((!repeat) ? msleep(12) : msleep(8));       // if transmit fails, waiting proper time to try again.
            cnt ++;
            if(frame_time_idx != 0) {       // if frame_time_idx > 0, means the bus is busy
                break;
            }
            if((frame_time_idx == 0) && (cnt > 33)) {       // test 1 second, if always low, return
                pr_err("CEC: bus error, always low\n");
                return ;
            }
        }
        frame_time_idx = 0;
        //msleep(17);             // judge whether cec bus is busy
        cec_repeat_flag ? msleep(17) : ((!repeat) ? msleep(12) : msleep(8));       // if transmit fails, waiting proper time to try again.
        cnt ++;
        if(cnt & (1 << 7)) {
            frame_time_idx = 0;
            pr_err("CEC: bus busy\n");
            return;
        }
    } while (frame_time_idx);   // frame_time_idx > 0, means that cec line is working
}

// return value: 1: successful      0: error
static int cec_ll_tx_once(const unsigned char *msg, unsigned char len)
{
    int i;
    unsigned int ret = 0xf;
    unsigned int n;
    int pos;
    
    cec_tx_start = 1;
    cec_rx_start = 1;
    get_bus_free();
    cec_rx_start = 0;
    
    for (i = 0; i < len; i++) {
     hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_0_HEADER + i, msg[i]);
    }
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_LENGTH, len-1);

    //cec_tx_start = 1;
    frame_time_idx = 0;
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_CMD, TX_REQ_CURRENT);//TX_REQ_NEXT
    msleep(len * 24 + 5);

    ret = hdmi_rd_reg(CEC0_BASE_ADDR+CEC_TX_MSG_STATUS);
    
    printk("cec TX status: rx: 0x%x; tx: 0x%x\n", hdmi_rd_reg(CEC0_BASE_ADDR+CEC_RX_MSG_STATUS), hdmi_rd_reg(CEC0_BASE_ADDR+CEC_TX_MSG_STATUS));
    if(ret == TX_DONE)
        ret = 1;
    else
        ret = 0;

    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_CMD, TX_NO_OP);
    cec_tx_start = 0;
    
    if(cec_msg_dbg_en == 1) {
        pos = 0;
        pos += sprintf(msg_log_buf + pos, "CEC: tx msg len: %d   dat: ", len);
        for(n = 0; n < len; n++) {
            pos += sprintf(msg_log_buf + pos, "%02x ", msg[n]);
        }
        pos += sprintf(msg_log_buf + pos, "\nCEC: tx state: %d\n", ret);
        msg_log_buf[pos] = '\0';
        printk("%s", msg_log_buf);
    }
    return ret;
}

// Return value: 0: fail    1: success
int cec_ll_tx(const unsigned char *msg, unsigned char len)
{
    
    int ret = 0;
    int i;

    mutex_lock(&cec_mutex);
    
    repeat = 0;
    memset(&cec_msg_bak, 0, sizeof(cec_msg_bak));
    memset(ack_check_point, 0, sizeof(ack_check_point));

    // save msg
    cec_msg_bak.len = len;
    for(i = 0; i < len; i++) {
        cec_msg_bak.msg[i] = msg[i];
        ack_check_point[i] = (i + 1) * 20 + 1;
    }

    // if transmit message error, try repeat(4) times
    do {     
        ret = cec_ll_tx_once(msg, len);
        cec_repeat_flag = ret;
        repeat ++;
        if(repeat > 1)
            cec_hw_reset();
    } while((ret == 0) && (repeat < 3));

    cec_msg_dbg_en ? printk("cec: ret = %d\n", ret) : 0;
    if(repeat > 1) {
        printk("cec: try %d times\n", repeat);
    }
    mutex_unlock(&cec_mutex);
    return ret;
}

static inline int get_value(unsigned time_rise, unsigned time_fall)
{
    unsigned time;
    if(time_rise > time_fall)
        time = time_rise - time_fall;
    else
        time = ((unsigned int)(0xffffffff)) - time_fall + time_rise;
    if((time > 1300) && (time < 1700))
        return 0;
    if((time > 400) && (time < 800))
        return 1;
    if((time > 2400) && (time < 3500))
        return 2;
    if((time > 3500) && (time < 3900))
        return 3;
    return -1;
}

static inline int need_check_ack(unsigned idx)
{
    int i;
    for(i = 0; (ack_check_point[i]) && (i < 16) ; i++) {
        if(ack_check_point[i] == idx) {
            return 1;
        }
    }
    return 0;
}

static void cec_gpi_receive_bits(void)
{
    int val = 0;

    aml_set_reg32_bits(P_MEDIA_CPU_IRQ_IN2_INTR_STAT_CLR, 1, 0, 1); // Write 1 to clear irq.
    aml_set_reg32_bits(P_GPIO_INTR_EDGE_POL, BUS_LEVEL(), 16, 1);
    if(!cec_tx_start) //Fiq for tx & arbitration only.
        return;
    frame_time_log[frame_time_idx] = aml_read_reg32(P_ISA_TIMERE);

    // ack rising position
    val = get_value(frame_time_log[frame_time_idx], frame_time_log[frame_time_idx - 1]);
    if(3 == val)
        frame_time_idx = 1;
    
    if((!cec_rx_start) && (val == 2) && (frame_time_idx > 20)) {
        hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_CMD, TX_ABORT); // stop cec tx for hw retry.
        hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_CMD, TX_NO_OP);
    }
    
    if((!cec_rx_start) && need_check_ack(frame_time_idx)) {
        // if val == 1, and DES != 0xf
        // if val == 0, and DES == 0xf
        // means we need stop cec tx
        if(((val == 1) ? ((cec_msg_bak.msg[0] & 0xf) != 0xf) : ((cec_msg_bak.msg[0] & 0xf) == 0xf)) || (val == -1)) {
            hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_CMD, TX_ABORT); // stop cec tx for NACK.
            aml_set_reg32_bits(P_MEDIA_CPU_IRQ_IN2_INTR_STAT_CLR, 1, 0, 1); // Write 1 to clear irq.
            hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_MSG_CMD, TX_NO_OP);
            aml_set_reg32_bits(P_MEDIA_CPU_IRQ_IN2_INTR_STAT_CLR, 1, 0, 1); // Write 1 to clear irq.
        }
    }
    (511 == frame_time_idx) ? (frame_time_idx = 0) : frame_time_idx++;
}

void cec_gpi_init(void)
{
    extern hdmitx_dev_t * get_hdmitx_device(void);
    hdmitx_device = get_hdmitx_device();
    
    if((hdmitx_device->cec_init_ready == 0) || (hdmitx_device->hpd_state == 0)) {      // If no connect, return directly
        aml_set_reg32_bits(P_MEDIA_CPU_IRQ_IN2_INTR_STAT_CLR, 1, 0, 1); // Write 1 to clear irq.
        printk("CEC FIQ return.\n");
        return;
    }
    if(cec_global_info.cec_flag.cec_fiq_flag){ //This function run only once.
        return;
    }
    cec_global_info.cec_flag.cec_fiq_flag = 1;
    aml_set_reg32_bits(P_MEDIA_CPU_IRQ_IN2_INTR_MASK, 0, 0, 1);     // disable irq
    aml_set_reg32_bits(P_MEDIA_CPU_IRQ_IN2_INTR_STAT_CLR, 1, 0, 1); // Write 1 to clear irq

    aml_set_reg32_bits(P_GPIO_INTR_GPIO_SEL0, 0x76, 0, 8);      // set GPIOC_23 as GPIO IRQ #0 source
    aml_set_reg32_bits(P_GPIO_INTR_EDGE_POL, 1, 0, 1);          // interrupt mode:  0: level     1: edge 
    aml_set_reg32_bits(P_GPIO_INTR_EDGE_POL, 1, 16, 1);
    request_fiq(INT_GPIO_0, &cec_gpi_receive_bits);
    printk("cec: register fiq\n");

    aml_set_reg32_bits(P_MEDIA_CPU_IRQ_IN2_INTR_MASK, 1, 0, 1);     // enable irq
    aml_set_reg32_bits(P_MEDIA_CPU_IRQ_IN2_INTR_STAT_CLR, 1, 0, 1); // Write 1 to clear irq
}

// DELETE LATER, TEST ONLY
void cec_test_(unsigned int cmd)
{
    printk("CEC: bus level: %s\n", BUS_LEVEL() ? "High" : "Low");
}
