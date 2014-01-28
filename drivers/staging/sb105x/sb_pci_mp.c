#include "sb_pci_mp.h"
#include <linux/module.h>
#include <linux/parport.h>

extern struct parport *parport_pc_probe_port(unsigned long base_lo,
		unsigned long base_hi,
		int irq, int dma,
		struct device *dev,
		int irqflags);

static struct mp_device_t mp_devs[MAX_MP_DEV];
static int mp_nrpcibrds = sizeof(mp_pciboards)/sizeof(mppcibrd_t);
static int NR_BOARD=0;
static int NR_PORTS=0;
static struct mp_port multi_ports[MAX_MP_PORT];
static struct irq_info irq_lists[NR_IRQS];

static _INLINE_ unsigned int serial_in(struct mp_port *mtpt, int offset);
static _INLINE_ void serial_out(struct mp_port *mtpt, int offset, int value);
static _INLINE_ unsigned int read_option_register(struct mp_port *mtpt, int offset);
static int sb1054_get_register(struct sb_uart_port *port, int page, int reg);
static int sb1054_set_register(struct sb_uart_port *port, int page, int reg, int value);
static void SendATCommand(struct mp_port *mtpt);
static int set_deep_fifo(struct sb_uart_port *port, int status);
static int get_deep_fifo(struct sb_uart_port *port);
static int get_device_type(int arg);
static int set_auto_rts(struct sb_uart_port *port, int status);
static void mp_stop(struct tty_struct *tty);
static void __mp_start(struct tty_struct *tty);
static void mp_start(struct tty_struct *tty);
static void mp_tasklet_action(unsigned long data);
static inline void mp_update_mctrl(struct sb_uart_port *port, unsigned int set, unsigned int clear);
static int mp_startup(struct sb_uart_state *state, int init_hw);
static void mp_shutdown(struct sb_uart_state *state);
static void mp_change_speed(struct sb_uart_state *state, struct MP_TERMIOS *old_termios);

static inline int __mp_put_char(struct sb_uart_port *port, struct circ_buf *circ, unsigned char c);
static int mp_put_char(struct tty_struct *tty, unsigned char ch);

static void mp_put_chars(struct tty_struct *tty);
static int mp_write(struct tty_struct *tty, const unsigned char *buf, int count);
static int mp_write_room(struct tty_struct *tty);
static int mp_chars_in_buffer(struct tty_struct *tty);
static void mp_flush_buffer(struct tty_struct *tty);
static void mp_send_xchar(struct tty_struct *tty, char ch);
static void mp_throttle(struct tty_struct *tty);
static void mp_unthrottle(struct tty_struct *tty);
static int mp_get_info(struct sb_uart_state *state, struct serial_struct *retinfo);
static int mp_set_info(struct sb_uart_state *state, struct serial_struct *newinfo);
static int mp_get_lsr_info(struct sb_uart_state *state, unsigned int *value);

static int mp_tiocmget(struct tty_struct *tty);
static int mp_tiocmset(struct tty_struct *tty, unsigned int set, unsigned int clear);
static int mp_break_ctl(struct tty_struct *tty, int break_state);
static int mp_do_autoconfig(struct sb_uart_state *state);
static int mp_wait_modem_status(struct sb_uart_state *state, unsigned long arg);
static int mp_get_count(struct sb_uart_state *state, struct serial_icounter_struct *icnt);
static int mp_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg);
static void mp_set_termios(struct tty_struct *tty, struct MP_TERMIOS *old_termios);
static void mp_close(struct tty_struct *tty, struct file *filp);
static void mp_wait_until_sent(struct tty_struct *tty, int timeout);
static void mp_hangup(struct tty_struct *tty);
static void mp_update_termios(struct sb_uart_state *state);
static int mp_block_til_ready(struct file *filp, struct sb_uart_state *state);
static struct sb_uart_state *uart_get(struct uart_driver *drv, int line);
static int mp_open(struct tty_struct *tty, struct file *filp);
static const char *mp_type(struct sb_uart_port *port);
static void mp_change_pm(struct sb_uart_state *state, int pm_state);
static inline void mp_report_port(struct uart_driver *drv, struct sb_uart_port *port);
static void mp_configure_port(struct uart_driver *drv, struct sb_uart_state *state, struct sb_uart_port *port);
static void mp_unconfigure_port(struct uart_driver *drv, struct sb_uart_state *state);
static int mp_register_driver(struct uart_driver *drv);
static void mp_unregister_driver(struct uart_driver *drv);
static int mp_add_one_port(struct uart_driver *drv, struct sb_uart_port *port);
static int mp_remove_one_port(struct uart_driver *drv, struct sb_uart_port *port);
static void autoconfig(struct mp_port *mtpt, unsigned int probeflags);
static void autoconfig_irq(struct mp_port *mtpt);
static void multi_stop_tx(struct sb_uart_port *port);
static void multi_start_tx(struct sb_uart_port *port);
static void multi_stop_rx(struct sb_uart_port *port);
static void multi_enable_ms(struct sb_uart_port *port);
static _INLINE_ void receive_chars(struct mp_port *mtpt, int *status );
static _INLINE_ void transmit_chars(struct mp_port *mtpt);
static _INLINE_ void check_modem_status(struct mp_port *mtpt);
static inline void multi_handle_port(struct mp_port *mtpt);
static irqreturn_t multi_interrupt(int irq, void *dev_id);
static void serial_do_unlink(struct irq_info *i, struct mp_port *mtpt);
static int serial_link_irq_chain(struct mp_port *mtpt);
static void serial_unlink_irq_chain(struct mp_port *mtpt);
static void multi_timeout(unsigned long data);
static unsigned int multi_tx_empty(struct sb_uart_port *port);
static unsigned int multi_get_mctrl(struct sb_uart_port *port);
static void multi_set_mctrl(struct sb_uart_port *port, unsigned int mctrl);
static void multi_break_ctl(struct sb_uart_port *port, int break_state);
static int multi_startup(struct sb_uart_port *port);
static void multi_shutdown(struct sb_uart_port *port);
static unsigned int multi_get_divisor(struct sb_uart_port *port, unsigned int baud);
static void multi_set_termios(struct sb_uart_port *port, struct MP_TERMIOS *termios, struct MP_TERMIOS *old);
static void multi_pm(struct sb_uart_port *port, unsigned int state, unsigned int oldstate);
static void multi_release_std_resource(struct mp_port *mtpt);
static void multi_release_port(struct sb_uart_port *port);
static int multi_request_port(struct sb_uart_port *port);
static void multi_config_port(struct sb_uart_port *port, int flags);
static int multi_verify_port(struct sb_uart_port *port, struct serial_struct *ser);
static const char *multi_type(struct sb_uart_port *port);
static void __init multi_init_ports(void);
static void __init multi_register_ports(struct uart_driver *drv);
static int init_mp_dev(struct pci_dev *pcidev, mppcibrd_t brd);

static int deep[256];
static int deep_count;
static int fcr_arr[256];
static int fcr_count;
static int ttr[256];
static int ttr_count;
static int rtr[256];
static int rtr_count;

module_param_array(deep,int,&deep_count,0);
module_param_array(fcr_arr,int,&fcr_count,0);
module_param_array(ttr,int,&ttr_count,0);
module_param_array(rtr,int,&rtr_count,0);

static _INLINE_ unsigned int serial_in(struct mp_port *mtpt, int offset)
{
	return inb(mtpt->port.iobase + offset);
}

static _INLINE_ void serial_out(struct mp_port *mtpt, int offset, int value)
{
	outb(value, mtpt->port.iobase + offset);
}

static _INLINE_ unsigned int read_option_register(struct mp_port *mtpt, int offset)
{
	return inb(mtpt->option_base_addr + offset);
}

static int sb1053a_get_interface(struct mp_port *mtpt, int port_num)
{
	unsigned long option_base_addr = mtpt->option_base_addr;
	unsigned int  interface = 0;

	switch (port_num)
	{
		case 0:
		case 1:
			/* set GPO[1:0] = 00 */
			outb(0x00, option_base_addr + MP_OPTR_GPODR);
			break;
		case 2:
		case 3:
			/* set GPO[1:0] = 01 */
			outb(0x01, option_base_addr + MP_OPTR_GPODR);
			break;
		case 4:
		case 5:
			/* set GPO[1:0] = 10 */
			outb(0x02, option_base_addr + MP_OPTR_GPODR);
			break;
		default:
			break;
	}

	port_num &= 0x1;

	/* get interface */
	interface = inb(option_base_addr + MP_OPTR_IIR0 + port_num);

	/* set GPO[1:0] = 11 */
	outb(0x03, option_base_addr + MP_OPTR_GPODR);

	return (interface);
}
		
static int sb1054_get_register(struct sb_uart_port *port, int page, int reg)
{
	int ret = 0;
	unsigned int lcr = 0;
	unsigned int mcr = 0;
	unsigned int tmp = 0;

	if( page <= 0)
	{
		printk(" page 0 can not use this fuction\n");
		return -1;
	}

	switch(page)
	{
		case 1:
			lcr = SB105X_GET_LCR(port);
			tmp = lcr | SB105X_LCR_DLAB;
			SB105X_PUT_LCR(port, tmp);

			tmp = SB105X_GET_LCR(port);

			ret = SB105X_GET_REG(port,reg);
			SB105X_PUT_LCR(port,lcr);
			break;
		case 2:
			mcr = SB105X_GET_MCR(port);
			tmp = mcr | SB105X_MCR_P2S;
			SB105X_PUT_MCR(port,tmp);

			ret = SB105X_GET_REG(port,reg);

			SB105X_PUT_MCR(port,mcr);
			break;
		case 3:
			lcr = SB105X_GET_LCR(port);
			tmp = lcr | SB105X_LCR_BF;
			SB105X_PUT_LCR(port,tmp);
			SB105X_PUT_REG(port,SB105X_PSR,SB105X_PSR_P3KEY);

			ret = SB105X_GET_REG(port,reg);

			SB105X_PUT_LCR(port,lcr);
			break;
		case 4:
			lcr = SB105X_GET_LCR(port);
			tmp = lcr | SB105X_LCR_BF;
			SB105X_PUT_LCR(port,tmp);
			SB105X_PUT_REG(port,SB105X_PSR,SB105X_PSR_P4KEY);

			ret = SB105X_GET_REG(port,reg);

			SB105X_PUT_LCR(port,lcr);
			break;
		default:
			printk(" error invalid page number \n");
			return -1;
	}

	return ret;
}

static int sb1054_set_register(struct sb_uart_port *port, int page, int reg, int value)
{  
	int lcr = 0;
	int mcr = 0;
	int ret = 0;

	if( page <= 0)
	{
		printk(" page 0 can not use this fuction\n");
		return -1;
	}
	switch(page)
	{
		case 1:
			lcr = SB105X_GET_LCR(port);
			SB105X_PUT_LCR(port, lcr | SB105X_LCR_DLAB);

			SB105X_PUT_REG(port,reg,value);

			SB105X_PUT_LCR(port, lcr);
			ret = 1;
			break;
		case 2:
			mcr = SB105X_GET_MCR(port);
			SB105X_PUT_MCR(port, mcr | SB105X_MCR_P2S);

			SB105X_PUT_REG(port,reg,value);

			SB105X_PUT_MCR(port, mcr);
			ret = 1;
			break;
		case 3:
			lcr = SB105X_GET_LCR(port);
			SB105X_PUT_LCR(port, lcr | SB105X_LCR_BF);
			SB105X_PUT_PSR(port, SB105X_PSR_P3KEY);

			SB105X_PUT_REG(port,reg,value);

			SB105X_PUT_LCR(port, lcr);
			ret = 1;
			break;
		case 4:
			lcr = SB105X_GET_LCR(port);
			SB105X_PUT_LCR(port, lcr | SB105X_LCR_BF);
			SB105X_PUT_PSR(port, SB105X_PSR_P4KEY);

			SB105X_PUT_REG(port,reg,value);

			SB105X_PUT_LCR(port, lcr);
			ret = 1;
			break;
		default:
			printk(" error invalid page number \n");
			return -1;
	}

	return ret;
}

static int set_multidrop_mode(struct sb_uart_port *port, unsigned int mode)
{
	int mdr = SB105XA_MDR_NPS;

	if (mode & MDMODE_ENABLE)
	{
		mdr |= SB105XA_MDR_MDE;
	}

	if (1) //(mode & MDMODE_AUTO)
	{
		int efr = 0;
		mdr |= SB105XA_MDR_AME;
		efr = sb1054_get_register(port, PAGE_3, SB105X_EFR);
		efr |= SB105X_EFR_SCD;
		sb1054_set_register(port, PAGE_3, SB105X_EFR, efr);
	}

	sb1054_set_register(port, PAGE_1, SB105XA_MDR, mdr);
	port->mdmode &= ~0x6;
	port->mdmode |= mode;
	printk("[%d] multidrop init: %x\n", port->line, port->mdmode);

	return 0;
}

static int get_multidrop_addr(struct sb_uart_port *port)
{
	return sb1054_get_register(port, PAGE_3, SB105X_XOFF2);
}

static int set_multidrop_addr(struct sb_uart_port *port, unsigned int addr)
{
	sb1054_set_register(port, PAGE_3, SB105X_XOFF2, addr);

	return 0;
}

static void SendATCommand(struct mp_port *mtpt)
{
	//		      a    t	cr   lf
	unsigned char ch[] = {0x61,0x74,0x0d,0x0a,0x0};
	unsigned char lineControl;
	unsigned char i=0;
	unsigned char Divisor = 0xc;

	lineControl = serial_inp(mtpt,UART_LCR);
	serial_outp(mtpt,UART_LCR,(lineControl | UART_LCR_DLAB));
	serial_outp(mtpt,UART_DLL,(Divisor & 0xff));
	serial_outp(mtpt,UART_DLM,(Divisor & 0xff00)>>8); //baudrate is 4800


	serial_outp(mtpt,UART_LCR,lineControl);	
	serial_outp(mtpt,UART_LCR,0x03); // N-8-1
	serial_outp(mtpt,UART_FCR,7); 
	serial_outp(mtpt,UART_MCR,0x3);
	while(ch[i]){
		while((serial_inp(mtpt,UART_LSR) & 0x60) !=0x60){
			;
		}
		serial_outp(mtpt,0,ch[i++]);
	}


}// end of SendATCommand()

static int set_deep_fifo(struct sb_uart_port *port, int status)
{
	int afr_status = 0;
	afr_status = sb1054_get_register(port, PAGE_4, SB105X_AFR);

	if(status == ENABLE)
	{
		afr_status |= SB105X_AFR_AFEN;
	}
	else
	{
		afr_status &= ~SB105X_AFR_AFEN;
	}
		
	sb1054_set_register(port,PAGE_4,SB105X_AFR,afr_status);
	sb1054_set_register(port,PAGE_4,SB105X_TTR,ttr[port->line]); 
	sb1054_set_register(port,PAGE_4,SB105X_RTR,rtr[port->line]); 
	afr_status = sb1054_get_register(port, PAGE_4, SB105X_AFR);
		
	return afr_status;
}

static int get_device_type(int arg)
{
	int ret;
        ret = inb(mp_devs[arg].option_reg_addr+MP_OPTR_DIR0);
        ret = (ret & 0xf0) >> 4;
        switch (ret)
        {
               case DIR_UART_16C550:
                    return PORT_16C55X;
               case DIR_UART_16C1050:
                    return PORT_16C105X;
               case DIR_UART_16C1050A:
               /*
               if (mtpt->port.line < 2)
               {
                    return PORT_16C105XA;
               }
               else
               {
                   if (mtpt->device->device_id & 0x50)
                   {
                       return PORT_16C55X;
                   }
                   else
                   {
                       return PORT_16C105X;
                   }
               }*/
               return PORT_16C105XA;
               default:
                    return PORT_UNKNOWN;
        }

}
static int get_deep_fifo(struct sb_uart_port *port)
{
	int afr_status = 0;
	afr_status = sb1054_get_register(port, PAGE_4, SB105X_AFR);
	return afr_status;
}

static int set_auto_rts(struct sb_uart_port *port, int status)
{
	int atr_status = 0;

#if 0
	int efr_status = 0;

	efr_status = sb1054_get_register(port, PAGE_3, SB105X_EFR);
	if(status == ENABLE)
		efr_status |= SB105X_EFR_ARTS;
	else
		efr_status &= ~SB105X_EFR_ARTS;
	sb1054_set_register(port,PAGE_3,SB105X_EFR,efr_status);
	efr_status = sb1054_get_register(port, PAGE_3, SB105X_EFR);
#endif
		
//ATR
	atr_status = sb1054_get_register(port, PAGE_3, SB105X_ATR);
	switch(status)
	{
		case RS422PTP:
			atr_status = (SB105X_ATR_TPS) | (SB105X_ATR_A80);
			break;
		case RS422MD:
			atr_status = (SB105X_ATR_TPS) | (SB105X_ATR_TCMS) | (SB105X_ATR_A80);
			break;
		case RS485NE:
			atr_status = (SB105X_ATR_RCMS) | (SB105X_ATR_TPS) | (SB105X_ATR_TCMS) | (SB105X_ATR_A80);
			break;
		case RS485ECHO:
			atr_status = (SB105X_ATR_TPS) | (SB105X_ATR_TCMS) | (SB105X_ATR_A80);
			break;
	}

	sb1054_set_register(port,PAGE_3,SB105X_ATR,atr_status);
	atr_status = sb1054_get_register(port, PAGE_3, SB105X_ATR);

	return atr_status;
}

static void mp_stop(struct tty_struct *tty)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port = state->port;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	port->ops->stop_tx(port);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void __mp_start(struct tty_struct *tty)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port = state->port;

	if (!uart_circ_empty(&state->info->xmit) && state->info->xmit.buf &&
			!tty->stopped && !tty->hw_stopped)
		port->ops->start_tx(port);
}

static void mp_start(struct tty_struct *tty)
{
	__mp_start(tty);
}

static void mp_tasklet_action(unsigned long data)
{
	struct sb_uart_state *state = (struct sb_uart_state *)data;
	struct tty_struct *tty;

	printk("tasklet is called!\n");
	tty = state->info->tty;
	tty_wakeup(tty);
}

static inline void mp_update_mctrl(struct sb_uart_port *port, unsigned int set, unsigned int clear)
{
	unsigned int old;

	old = port->mctrl;
	port->mctrl = (old & ~clear) | set;
	if (old != port->mctrl)
		port->ops->set_mctrl(port, port->mctrl);
}

#define uart_set_mctrl(port,set)	mp_update_mctrl(port,set,0)
#define uart_clear_mctrl(port,clear)	mp_update_mctrl(port,0,clear)

static int mp_startup(struct sb_uart_state *state, int init_hw)
{
	struct sb_uart_info *info = state->info;
	struct sb_uart_port *port = state->port;
	unsigned long page;
	int retval = 0;

	if (info->flags & UIF_INITIALIZED)
		return 0;

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	if (port->type == PORT_UNKNOWN)
		return 0;

	if (!info->xmit.buf) {
		page = get_zeroed_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;

		info->xmit.buf = (unsigned char *) page;
			
		uart_circ_clear(&info->xmit);
	}

	retval = port->ops->startup(port);
	if (retval == 0) {
		if (init_hw) {
			mp_change_speed(state, NULL);

			if (info->tty && (info->tty->termios.c_cflag & CBAUD))
				uart_set_mctrl(port, TIOCM_RTS | TIOCM_DTR);
		}

		info->flags |= UIF_INITIALIZED;

		if (info->tty)
			clear_bit(TTY_IO_ERROR, &info->tty->flags);
	}

	if (retval && capable(CAP_SYS_ADMIN))
		retval = 0;

	return retval;
}

static void mp_shutdown(struct sb_uart_state *state)
{
	struct sb_uart_info *info = state->info;
	struct sb_uart_port *port = state->port;

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	if (info->flags & UIF_INITIALIZED) {
		info->flags &= ~UIF_INITIALIZED;

		if (!info->tty || (info->tty->termios.c_cflag & HUPCL))
			uart_clear_mctrl(port, TIOCM_DTR | TIOCM_RTS);

		wake_up_interruptible(&info->delta_msr_wait);

		port->ops->shutdown(port);

		synchronize_irq(port->irq);
	}
	tasklet_kill(&info->tlet);

	if (info->xmit.buf) {
		free_page((unsigned long)info->xmit.buf);
		info->xmit.buf = NULL;
	}
}

static void mp_change_speed(struct sb_uart_state *state, struct MP_TERMIOS *old_termios)
{
	struct tty_struct *tty = state->info->tty;
	struct sb_uart_port *port = state->port;

	if (!tty || port->type == PORT_UNKNOWN)
		return;

	if (tty->termios.c_cflag & CRTSCTS)
		state->info->flags |= UIF_CTS_FLOW;
	else
		state->info->flags &= ~UIF_CTS_FLOW;

	if (tty->termios.c_cflag & CLOCAL)
		state->info->flags &= ~UIF_CHECK_CD;
	else
		state->info->flags |= UIF_CHECK_CD;

	port->ops->set_termios(port, &tty->termios, old_termios);
}

static inline int __mp_put_char(struct sb_uart_port *port, struct circ_buf *circ, unsigned char c)
{
	unsigned long flags;
	int ret = 0;

	if (!circ->buf)
		return 0;

	spin_lock_irqsave(&port->lock, flags);
	if (uart_circ_chars_free(circ) != 0) {
		circ->buf[circ->head] = c;
		circ->head = (circ->head + 1) & (UART_XMIT_SIZE - 1);
		ret = 1;
	}
	spin_unlock_irqrestore(&port->lock, flags);
	return ret;
}

static int mp_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct sb_uart_state *state = tty->driver_data;

	return __mp_put_char(state->port, &state->info->xmit, ch);
}

static void mp_put_chars(struct tty_struct *tty)
{
	mp_start(tty);
}

static int mp_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port;
	struct circ_buf *circ;
	int c, ret = 0;

	if (!state || !state->info) {
		return -EL3HLT;
	}

	port = state->port;
	circ = &state->info->xmit;

	if (!circ->buf)
		return 0;
		
	while (1) {
		c = CIRC_SPACE_TO_END(circ->head, circ->tail, UART_XMIT_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;
	memcpy(circ->buf + circ->head, buf, c);

		circ->head = (circ->head + c) & (UART_XMIT_SIZE - 1);
		buf += c;
		count -= c;
		ret += c;
	}
	mp_start(tty);
	return ret;
}

static int mp_write_room(struct tty_struct *tty)
{
	struct sb_uart_state *state = tty->driver_data;

	return uart_circ_chars_free(&state->info->xmit);
}

static int mp_chars_in_buffer(struct tty_struct *tty)
{
	struct sb_uart_state *state = tty->driver_data;

	return uart_circ_chars_pending(&state->info->xmit);
}

static void mp_flush_buffer(struct tty_struct *tty)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port;
	unsigned long flags;

	if (!state || !state->info) {
		return;
	}

	port = state->port;
	spin_lock_irqsave(&port->lock, flags);
	uart_circ_clear(&state->info->xmit);
	spin_unlock_irqrestore(&port->lock, flags);
	wake_up_interruptible(&tty->write_wait);
	tty_wakeup(tty);
}

static void mp_send_xchar(struct tty_struct *tty, char ch)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port = state->port;
	unsigned long flags;

	if (port->ops->send_xchar)
		port->ops->send_xchar(port, ch);
	else {
		port->x_char = ch;
		if (ch) {
			spin_lock_irqsave(&port->lock, flags);
			port->ops->start_tx(port);
			spin_unlock_irqrestore(&port->lock, flags);
		}
	}
}

static void mp_throttle(struct tty_struct *tty)
{
	struct sb_uart_state *state = tty->driver_data;

	if (I_IXOFF(tty))
		mp_send_xchar(tty, STOP_CHAR(tty));

	if (tty->termios.c_cflag & CRTSCTS)
		uart_clear_mctrl(state->port, TIOCM_RTS);
}

static void mp_unthrottle(struct tty_struct *tty)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port = state->port;

	if (I_IXOFF(tty)) {
		if (port->x_char)
			port->x_char = 0;
		else
			mp_send_xchar(tty, START_CHAR(tty));
	}

	if (tty->termios.c_cflag & CRTSCTS)
		uart_set_mctrl(port, TIOCM_RTS);
}

static int mp_get_info(struct sb_uart_state *state, struct serial_struct *retinfo)
{
	struct sb_uart_port *port = state->port;
	struct serial_struct tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.type	    = port->type;
	tmp.line	    = port->line;
	tmp.port	    = port->iobase;
	if (HIGH_BITS_OFFSET)
		tmp.port_high = (long) port->iobase >> HIGH_BITS_OFFSET;
	tmp.irq		    = port->irq;
	tmp.flags	    = port->flags;
	tmp.xmit_fifo_size  = port->fifosize;
	tmp.baud_base	    = port->uartclk / 16;
	tmp.close_delay	    = state->close_delay;
	tmp.closing_wait    = state->closing_wait == USF_CLOSING_WAIT_NONE ?
		ASYNC_CLOSING_WAIT_NONE :
		state->closing_wait;
	tmp.custom_divisor  = port->custom_divisor;
	tmp.hub6	    = port->hub6;
	tmp.io_type         = port->iotype;
	tmp.iomem_reg_shift = port->regshift;
	tmp.iomem_base      = (void *)port->mapbase;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int mp_set_info(struct sb_uart_state *state, struct serial_struct *newinfo)
{
	struct serial_struct new_serial;
	struct sb_uart_port *port = state->port;
	unsigned long new_port;
	unsigned int change_irq, change_port, closing_wait;
	unsigned int old_custom_divisor;
	unsigned int old_flags, new_flags;
	int retval = 0;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;

	new_port = new_serial.port;
	if (HIGH_BITS_OFFSET)
		new_port += (unsigned long) new_serial.port_high << HIGH_BITS_OFFSET;

	new_serial.irq = irq_canonicalize(new_serial.irq);

	closing_wait = new_serial.closing_wait == ASYNC_CLOSING_WAIT_NONE ?
		USF_CLOSING_WAIT_NONE : new_serial.closing_wait;
	MP_STATE_LOCK(state);

	change_irq  = new_serial.irq != port->irq;
	change_port = new_port != port->iobase ||
		(unsigned long)new_serial.iomem_base != port->mapbase ||
		new_serial.hub6 != port->hub6 ||
		new_serial.io_type != port->iotype ||
		new_serial.iomem_reg_shift != port->regshift ||
		new_serial.type != port->type;
	old_flags = port->flags;
	new_flags = new_serial.flags;
	old_custom_divisor = port->custom_divisor;

	if (!capable(CAP_SYS_ADMIN)) {
		retval = -EPERM;
		if (change_irq || change_port ||
				(new_serial.baud_base != port->uartclk / 16) ||
				(new_serial.close_delay != state->close_delay) ||
				(closing_wait != state->closing_wait) ||
				(new_serial.xmit_fifo_size != port->fifosize) ||
				(((new_flags ^ old_flags) & ~UPF_USR_MASK) != 0))
			goto exit;
		port->flags = ((port->flags & ~UPF_USR_MASK) |
				(new_flags & UPF_USR_MASK));
		port->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	if (port->ops->verify_port)
		retval = port->ops->verify_port(port, &new_serial);

	if ((new_serial.irq >= NR_IRQS) || (new_serial.irq < 0) ||
			(new_serial.baud_base < 9600))
		retval = -EINVAL;

	if (retval)
		goto exit;

	if (change_port || change_irq) {
		retval = -EBUSY;

		if (uart_users(state) > 1)
			goto exit;

		mp_shutdown(state);
	}

	if (change_port) {
		unsigned long old_iobase, old_mapbase;
		unsigned int old_type, old_iotype, old_hub6, old_shift;

		old_iobase = port->iobase;
		old_mapbase = port->mapbase;
		old_type = port->type;
		old_hub6 = port->hub6;
		old_iotype = port->iotype;
		old_shift = port->regshift;

		if (old_type != PORT_UNKNOWN)
			port->ops->release_port(port);

		port->iobase = new_port;
		port->type = new_serial.type;
		port->hub6 = new_serial.hub6;
		port->iotype = new_serial.io_type;
		port->regshift = new_serial.iomem_reg_shift;
		port->mapbase = (unsigned long)new_serial.iomem_base;

		if (port->type != PORT_UNKNOWN) {
			retval = port->ops->request_port(port);
		} else {
			retval = 0;
		}

		if (retval && old_type != PORT_UNKNOWN) {
			port->iobase = old_iobase;
			port->type = old_type;
			port->hub6 = old_hub6;
			port->iotype = old_iotype;
			port->regshift = old_shift;
			port->mapbase = old_mapbase;
			retval = port->ops->request_port(port);
			if (retval)
				port->type = PORT_UNKNOWN;

			retval = -EBUSY;
		}
	}

	port->irq              = new_serial.irq;
	port->uartclk          = new_serial.baud_base * 16;
	port->flags            = (port->flags & ~UPF_CHANGE_MASK) |
		(new_flags & UPF_CHANGE_MASK);
	port->custom_divisor   = new_serial.custom_divisor;
	state->close_delay     = new_serial.close_delay;
	state->closing_wait    = closing_wait;
	port->fifosize         = new_serial.xmit_fifo_size;
	if (state->info->tty)
		state->info->tty->low_latency =
			(port->flags & UPF_LOW_LATENCY) ? 1 : 0;

check_and_exit:
	retval = 0;
	if (port->type == PORT_UNKNOWN)
		goto exit;
	if (state->info->flags & UIF_INITIALIZED) {
		if (((old_flags ^ port->flags) & UPF_SPD_MASK) ||
				old_custom_divisor != port->custom_divisor) {
			if (port->flags & UPF_SPD_MASK) {
				printk(KERN_NOTICE
						"%s sets custom speed on ttyMP%d. This "
						"is deprecated.\n", current->comm,
						port->line);
			}
			mp_change_speed(state, NULL);
		}
	} else
		retval = mp_startup(state, 1);
exit:
	MP_STATE_UNLOCK(state);
	return retval;
}


static int mp_get_lsr_info(struct sb_uart_state *state, unsigned int *value)
{
	struct sb_uart_port *port = state->port;
	unsigned int result;

	result = port->ops->tx_empty(port);

	if (port->x_char ||
			((uart_circ_chars_pending(&state->info->xmit) > 0) &&
				!state->info->tty->stopped && !state->info->tty->hw_stopped))
		result &= ~TIOCSER_TEMT;

	return put_user(result, value);
}

static int mp_tiocmget(struct tty_struct *tty)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port = state->port;
	int result = -EIO;

	MP_STATE_LOCK(state);
	if (!(tty->flags & (1 << TTY_IO_ERROR))) {
		result = port->mctrl;
		spin_lock_irq(&port->lock);
		result |= port->ops->get_mctrl(port);
		spin_unlock_irq(&port->lock);
	}
	MP_STATE_UNLOCK(state);
	return result;
}

static int mp_tiocmset(struct tty_struct *tty, unsigned int set, unsigned int clear)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port = state->port;
	int ret = -EIO;


	MP_STATE_LOCK(state);
	if (!(tty->flags & (1 << TTY_IO_ERROR))) {
		mp_update_mctrl(port, set, clear);
		ret = 0;
	}
	MP_STATE_UNLOCK(state);

	return ret;
}

static int mp_break_ctl(struct tty_struct *tty, int break_state)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port = state->port;

	MP_STATE_LOCK(state);

	if (port->type != PORT_UNKNOWN)
		port->ops->break_ctl(port, break_state);

	MP_STATE_UNLOCK(state);
	return 0;
}

static int mp_do_autoconfig(struct sb_uart_state *state)
{
	struct sb_uart_port *port = state->port;
	int flags, ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (mutex_lock_interruptible(&state->mutex))
		return -ERESTARTSYS;
	ret = -EBUSY;
	if (uart_users(state) == 1) {
		mp_shutdown(state);

		if (port->type != PORT_UNKNOWN)
			port->ops->release_port(port);

		flags = UART_CONFIG_TYPE;
		if (port->flags & UPF_AUTO_IRQ)
			flags |= UART_CONFIG_IRQ;

		port->ops->config_port(port, flags);

		ret = mp_startup(state, 1);
	}
	MP_STATE_UNLOCK(state);
	return ret;
}

static int mp_wait_modem_status(struct sb_uart_state *state, unsigned long arg)
{
	struct sb_uart_port *port = state->port;
	DECLARE_WAITQUEUE(wait, current);
	struct sb_uart_icount cprev, cnow;
	int ret;

	spin_lock_irq(&port->lock);
	memcpy(&cprev, &port->icount, sizeof(struct sb_uart_icount));

	port->ops->enable_ms(port);
	spin_unlock_irq(&port->lock);

	add_wait_queue(&state->info->delta_msr_wait, &wait);
	for (;;) {
		spin_lock_irq(&port->lock);
		memcpy(&cnow, &port->icount, sizeof(struct sb_uart_icount));
		spin_unlock_irq(&port->lock);

		set_current_state(TASK_INTERRUPTIBLE);

		if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
				((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
				((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
				((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
			ret = 0;
			break;
		}

		schedule();

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		cprev = cnow;
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&state->info->delta_msr_wait, &wait);

	return ret;
}

static int mp_get_count(struct sb_uart_state *state, struct serial_icounter_struct *icnt)
{
	struct serial_icounter_struct icount = {};
	struct sb_uart_icount cnow;
	struct sb_uart_port *port = state->port;

	spin_lock_irq(&port->lock);
	memcpy(&cnow, &port->icount, sizeof(struct sb_uart_icount));
	spin_unlock_irq(&port->lock);

	icount.cts         = cnow.cts;
	icount.dsr         = cnow.dsr;
	icount.rng         = cnow.rng;
	icount.dcd         = cnow.dcd;
	icount.rx          = cnow.rx;
	icount.tx          = cnow.tx;
	icount.frame       = cnow.frame;
	icount.overrun     = cnow.overrun;
	icount.parity      = cnow.parity;
	icount.brk         = cnow.brk;
	icount.buf_overrun = cnow.buf_overrun;

	return copy_to_user(icnt, &icount, sizeof(icount)) ? -EFAULT : 0;
}

static int mp_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	struct sb_uart_state *state = tty->driver_data;
	struct mp_port *info = (struct mp_port *)state->port;
	int ret = -ENOIOCTLCMD;


	switch (cmd) {
		case TIOCSMULTIDROP:
			/* set multi-drop mode enable or disable, and default operation mode is H/W mode */
			if (info->port.type == PORT_16C105XA)
			{
				//arg &= ~0x6;
				//state->port->mdmode = 0;
				return set_multidrop_mode((struct sb_uart_port *)info, (unsigned int)arg);
			}
			ret = -ENOTSUPP;
			break;
		case GETDEEPFIFO:
			ret = get_deep_fifo(state->port);
			return ret;
		case SETDEEPFIFO:
			ret = set_deep_fifo(state->port,arg);
			deep[state->port->line] = arg;
			return ret;
		case SETTTR:
			if (info->port.type == PORT_16C105X || info->port.type == PORT_16C105XA){
				ret = sb1054_set_register(state->port,PAGE_4,SB105X_TTR,arg);
				ttr[state->port->line] = arg;
			}
			return ret;
		case SETRTR:
			if (info->port.type == PORT_16C105X || info->port.type == PORT_16C105XA){
				ret = sb1054_set_register(state->port,PAGE_4,SB105X_RTR,arg);
				rtr[state->port->line] = arg;
			}
			return ret;
		case GETTTR:
			if (info->port.type == PORT_16C105X || info->port.type == PORT_16C105XA){
				ret = sb1054_get_register(state->port,PAGE_4,SB105X_TTR);
			}
			return ret;
		case GETRTR:
			if (info->port.type == PORT_16C105X || info->port.type == PORT_16C105XA){
				ret = sb1054_get_register(state->port,PAGE_4,SB105X_RTR);
			}
			return ret;

		case SETFCR:
			if (info->port.type == PORT_16C105X || info->port.type == PORT_16C105XA){
				ret = sb1054_set_register(state->port,PAGE_1,SB105X_FCR,arg);
			}
			else{
				serial_out(info,2,arg);
			}

			return ret;
		case TIOCSMDADDR:
			/* set multi-drop address */
			if (info->port.type == PORT_16C105XA)
			{
				state->port->mdmode |= MDMODE_ADDR;
				return set_multidrop_addr((struct sb_uart_port *)info, (unsigned int)arg);
			}
			ret = -ENOTSUPP;
			break;

		case TIOCGMDADDR:
			/* set multi-drop address */
			if ((info->port.type == PORT_16C105XA) && (state->port->mdmode & MDMODE_ADDR))
			{
				return get_multidrop_addr((struct sb_uart_port *)info);
			}
			ret = -ENOTSUPP;
			break;

		case TIOCSENDADDR:
			/* send address in multi-drop mode */
			if ((info->port.type == PORT_16C105XA) 
					&& (state->port->mdmode & (MDMODE_ENABLE)))
			{
				if (mp_chars_in_buffer(tty) > 0)
				{
					tty_wait_until_sent(tty, 0);
				}
				//while ((serial_in(info, UART_LSR) & 0x60) != 0x60);
				//while (sb1054_get_register(state->port, PAGE_2, SB105X_TFCR) != 0);
				while ((serial_in(info, UART_LSR) & 0x60) != 0x60);
				serial_out(info, UART_SCR, (int)arg);
			}
			break;

		case TIOCGSERIAL:
			ret = mp_get_info(state, (struct serial_struct *)arg);
			break;

		case TIOCSSERIAL:
			ret = mp_set_info(state, (struct serial_struct *)arg);
			break;

		case TIOCSERCONFIG:
			ret = mp_do_autoconfig(state);
			break;

		case TIOCSERGWILD: /* obsolete */
		case TIOCSERSWILD: /* obsolete */
			ret = 0;
			break;
			/* for Multiport */
		case TIOCGNUMOFPORT: /* Get number of ports */
			return NR_PORTS;
		case TIOCGGETDEVID:
			return mp_devs[arg].device_id;
		case TIOCGGETREV:
			return mp_devs[arg].revision;
		case TIOCGGETNRPORTS:
			return mp_devs[arg].nr_ports;
		case TIOCGGETBDNO:
			return NR_BOARD;
		case TIOCGGETINTERFACE:
			if (mp_devs[arg].revision == 0xc0)
			{
				/* for SB16C1053APCI */
				return (sb1053a_get_interface(info, info->port.line));
			}
			else
			{
				return (inb(mp_devs[arg].option_reg_addr+MP_OPTR_IIR0+(state->port->line/8)));
			}
		case TIOCGGETPORTTYPE:
			ret = get_device_type(arg);
			return ret;
		case TIOCSMULTIECHO: /* set to multi-drop mode(RS422) or echo mode(RS485)*/
			outb( ( inb(info->interface_config_addr) & ~0x03 ) | 0x01 ,  
					info->interface_config_addr);
			return 0;
		case TIOCSPTPNOECHO: /* set to multi-drop mode(RS422) or echo mode(RS485) */
			outb( ( inb(info->interface_config_addr) & ~0x03 )  ,             
					info->interface_config_addr);
			return 0;
	}

	if (ret != -ENOIOCTLCMD)
		goto out;

	if (tty->flags & (1 << TTY_IO_ERROR)) {
		ret = -EIO;
		goto out;
	}

	switch (cmd) {
		case TIOCMIWAIT:
			ret = mp_wait_modem_status(state, arg);
			break;

		case TIOCGICOUNT:
			ret = mp_get_count(state, (struct serial_icounter_struct *)arg);
			break;
	}

	if (ret != -ENOIOCTLCMD)
		goto out;

	MP_STATE_LOCK(state);
	switch (cmd) {
		case TIOCSERGETLSR: /* Get line status register */
			ret = mp_get_lsr_info(state, (unsigned int *)arg);
			break;

		default: {
					struct sb_uart_port *port = state->port;
					if (port->ops->ioctl)
						ret = port->ops->ioctl(port, cmd, arg);
					break;
				}
	}

	MP_STATE_UNLOCK(state);
out:
	return ret;
}

static void mp_set_termios(struct tty_struct *tty, struct MP_TERMIOS *old_termios)
{
	struct sb_uart_state *state = tty->driver_data;
	unsigned long flags;
	unsigned int cflag = tty->termios.c_cflag;

#define RELEVANT_IFLAG(iflag)	((iflag) & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

	if ((cflag ^ old_termios->c_cflag) == 0 &&
			RELEVANT_IFLAG(tty->termios.c_iflag ^ old_termios->c_iflag) == 0)
		return;

	mp_change_speed(state, old_termios);

	if ((old_termios->c_cflag & CBAUD) && !(cflag & CBAUD))
		uart_clear_mctrl(state->port, TIOCM_RTS | TIOCM_DTR);

	if (!(old_termios->c_cflag & CBAUD) && (cflag & CBAUD)) {
		unsigned int mask = TIOCM_DTR;
		if (!(cflag & CRTSCTS) ||
				!test_bit(TTY_THROTTLED, &tty->flags))
			mask |= TIOCM_RTS;
		uart_set_mctrl(state->port, mask);
	}

	if ((old_termios->c_cflag & CRTSCTS) && !(cflag & CRTSCTS)) {
		spin_lock_irqsave(&state->port->lock, flags);
		tty->hw_stopped = 0;
		__mp_start(tty);
		spin_unlock_irqrestore(&state->port->lock, flags);
	}

	if (!(old_termios->c_cflag & CRTSCTS) && (cflag & CRTSCTS)) {
		spin_lock_irqsave(&state->port->lock, flags);
		if (!(state->port->ops->get_mctrl(state->port) & TIOCM_CTS)) {
			tty->hw_stopped = 1;
			state->port->ops->stop_tx(state->port);
		}
		spin_unlock_irqrestore(&state->port->lock, flags);
	}
}

static void mp_close(struct tty_struct *tty, struct file *filp)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port;

	printk("mp_close!\n");
	if (!state || !state->port)
		return;

	port = state->port;

	printk("close1 %d\n", __LINE__);
	MP_STATE_LOCK(state);

	printk("close2 %d\n", __LINE__);
	if (tty_hung_up_p(filp))
		goto done;

	printk("close3 %d\n", __LINE__);
	if ((tty->count == 1) && (state->count != 1)) {
		printk("mp_close: bad serial port count; tty->count is 1, "
				"state->count is %d\n", state->count);
		state->count = 1;
	}
	printk("close4 %d\n", __LINE__);
	if (--state->count < 0) {
		printk("rs_close: bad serial port count for ttyMP%d: %d\n",
				port->line, state->count);
		state->count = 0;
	}
	if (state->count)
		goto done;

	tty->closing = 1;

	printk("close5 %d\n", __LINE__);
	if (state->closing_wait != USF_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, state->closing_wait);

	printk("close6 %d\n", __LINE__);
	if (state->info->flags & UIF_INITIALIZED) {
		unsigned long flags;
		spin_lock_irqsave(&port->lock, flags);
		port->ops->stop_rx(port);
		spin_unlock_irqrestore(&port->lock, flags);
		mp_wait_until_sent(tty, port->timeout);
	}
	printk("close7 %d\n", __LINE__);

	mp_shutdown(state);
	printk("close8 %d\n", __LINE__);
	mp_flush_buffer(tty);
	tty_ldisc_flush(tty);
	tty->closing = 0;
	state->info->tty = NULL;
	if (state->info->blocked_open) 
	{
		if (state->close_delay)
		{
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(state->close_delay);
		}
	}
	else
	{
		mp_change_pm(state, 3);
	}
	printk("close8 %d\n", __LINE__);

	state->info->flags &= ~UIF_NORMAL_ACTIVE;
	wake_up_interruptible(&state->info->open_wait);

done:
	printk("close done\n");
	MP_STATE_UNLOCK(state);
	module_put(THIS_MODULE);
}

static void mp_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct sb_uart_state *state = tty->driver_data;
	struct sb_uart_port *port = state->port;
	unsigned long char_time, expire;

	if (port->type == PORT_UNKNOWN || port->fifosize == 0)
		return;

	char_time = (port->timeout - HZ/50) / port->fifosize;
	char_time = char_time / 5;
	if (char_time == 0)
		char_time = 1;
	if (timeout && timeout < char_time)
		char_time = timeout;

	if (timeout == 0 || timeout > 2 * port->timeout)
		timeout = 2 * port->timeout;

	expire = jiffies + timeout;

	while (!port->ops->tx_empty(port)) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(char_time);
		if (signal_pending(current))
			break;
		if (time_after(jiffies, expire))
			break;
	}
	set_current_state(TASK_RUNNING); /* might not be needed */
}

static void mp_hangup(struct tty_struct *tty)
{
	struct sb_uart_state *state = tty->driver_data;

	MP_STATE_LOCK(state);
	if (state->info && state->info->flags & UIF_NORMAL_ACTIVE) {
		mp_flush_buffer(tty);
		mp_shutdown(state);
		state->count = 0;
		state->info->flags &= ~UIF_NORMAL_ACTIVE;
		state->info->tty = NULL;
		wake_up_interruptible(&state->info->open_wait);
		wake_up_interruptible(&state->info->delta_msr_wait);
	}
	MP_STATE_UNLOCK(state);
}

static void mp_update_termios(struct sb_uart_state *state)
{
	struct tty_struct *tty = state->info->tty;
	struct sb_uart_port *port = state->port;

	if (!(tty->flags & (1 << TTY_IO_ERROR))) {
		mp_change_speed(state, NULL);

		if (tty->termios.c_cflag & CBAUD)
			uart_set_mctrl(port, TIOCM_DTR | TIOCM_RTS);
	}
}

static int mp_block_til_ready(struct file *filp, struct sb_uart_state *state)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sb_uart_info *info = state->info;
	struct sb_uart_port *port = state->port;
	unsigned int mctrl;

	info->blocked_open++;
	state->count--;

	add_wait_queue(&info->open_wait, &wait);
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (tty_hung_up_p(filp) || info->tty == NULL)
			break;

		if (!(info->flags & UIF_INITIALIZED))
			break;

		if ((filp->f_flags & O_NONBLOCK) ||
				(info->tty->termios.c_cflag & CLOCAL) ||
				(info->tty->flags & (1 << TTY_IO_ERROR))) {
			break;
		}

		if (info->tty->termios.c_cflag & CBAUD)
			uart_set_mctrl(port, TIOCM_DTR);

		spin_lock_irq(&port->lock);
		port->ops->enable_ms(port);
		mctrl = port->ops->get_mctrl(port);
		spin_unlock_irq(&port->lock);
		if (mctrl & TIOCM_CAR)
			break;

		MP_STATE_UNLOCK(state);
		schedule();
		MP_STATE_LOCK(state);

		if (signal_pending(current))
			break;
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&info->open_wait, &wait);

	state->count++;
	info->blocked_open--;

	if (signal_pending(current))
		return -ERESTARTSYS;

	if (!info->tty || tty_hung_up_p(filp))
		return -EAGAIN;

	return 0;
}

static struct sb_uart_state *uart_get(struct uart_driver *drv, int line)
{
	struct sb_uart_state *state;

	MP_MUTEX_LOCK(mp_mutex);
	state = drv->state + line;
	if (mutex_lock_interruptible(&state->mutex)) {
		state = ERR_PTR(-ERESTARTSYS);
		goto out;
	}
	state->count++;
	if (!state->port) {
		state->count--;
		MP_STATE_UNLOCK(state);
		state = ERR_PTR(-ENXIO);
		goto out;
	}

	if (!state->info) {
		state->info = kmalloc(sizeof(struct sb_uart_info), GFP_KERNEL);
		if (state->info) {
			memset(state->info, 0, sizeof(struct sb_uart_info));
			init_waitqueue_head(&state->info->open_wait);
			init_waitqueue_head(&state->info->delta_msr_wait);

			state->port->info = state->info;

			tasklet_init(&state->info->tlet, mp_tasklet_action,
					(unsigned long)state);
		} else {
			state->count--;
			MP_STATE_UNLOCK(state);
			state = ERR_PTR(-ENOMEM);
		}
	}

out:
	MP_MUTEX_UNLOCK(mp_mutex);
	return state;
}

static int mp_open(struct tty_struct *tty, struct file *filp)
{
	struct uart_driver *drv = (struct uart_driver *)tty->driver->driver_state;
	struct sb_uart_state *state;
	int retval;
	int  line = tty->index;
	struct mp_port *mtpt;

	retval = -ENODEV;
	if (line >= tty->driver->num)
		goto fail;

	state = uart_get(drv, line);

	if (IS_ERR(state)) {
		retval = PTR_ERR(state);
		goto fail;
	}

	mtpt  = (struct mp_port *)state->port;

	tty->driver_data = state;
	tty->low_latency = (state->port->flags & UPF_LOW_LATENCY) ? 1 : 0;
	tty->alt_speed = 0;
	state->info->tty = tty;

	if (tty_hung_up_p(filp)) {
		retval = -EAGAIN;
		state->count--;
		MP_STATE_UNLOCK(state);
		goto fail;
	}

	if (state->count == 1)
		mp_change_pm(state, 0);

	retval = mp_startup(state, 0);

	if (retval == 0)
		retval = mp_block_til_ready(filp, state);
	MP_STATE_UNLOCK(state);

	if (retval == 0 && !(state->info->flags & UIF_NORMAL_ACTIVE)) {
		state->info->flags |= UIF_NORMAL_ACTIVE;

		mp_update_termios(state);
	}

	uart_clear_mctrl(state->port, TIOCM_RTS);
	try_module_get(THIS_MODULE);
fail:
	return retval;
}


static const char *mp_type(struct sb_uart_port *port)
{
	const char *str = NULL;

	if (port->ops->type)
		str = port->ops->type(port);

	if (!str)
		str = "unknown";

	return str;
}

static void mp_change_pm(struct sb_uart_state *state, int pm_state)
{
	struct sb_uart_port *port = state->port;
	if (port->ops->pm)
		port->ops->pm(port, pm_state, state->pm_state);
	state->pm_state = pm_state;
}

static inline void mp_report_port(struct uart_driver *drv, struct sb_uart_port *port)
{
	char address[64];

	switch (port->iotype) {
		case UPIO_PORT:
			snprintf(address, sizeof(address),"I/O 0x%x", port->iobase);
			break;
		case UPIO_HUB6:
			snprintf(address, sizeof(address),"I/O 0x%x offset 0x%x", port->iobase, port->hub6);
			break;
		case UPIO_MEM:
			snprintf(address, sizeof(address),"MMIO 0x%lx", port->mapbase);
			break;
		default:
			snprintf(address, sizeof(address),"*unknown*" );
			strlcpy(address, "*unknown*", sizeof(address));
			break;
	}

	printk( "%s%d at %s (irq = %d) is a %s\n",
			drv->dev_name, port->line, address, port->irq, mp_type(port));

}

static void mp_configure_port(struct uart_driver *drv, struct sb_uart_state *state, struct sb_uart_port *port)
{
	unsigned int flags;


	if (!port->iobase && !port->mapbase && !port->membase)
	{
		DPRINTK("%s error \n",__FUNCTION__);
		return;
	}
	flags = UART_CONFIG_TYPE;
	if (port->flags & UPF_AUTO_IRQ)
		flags |= UART_CONFIG_IRQ;
	if (port->flags & UPF_BOOT_AUTOCONF) {
		port->type = PORT_UNKNOWN;
		port->ops->config_port(port, flags);
	}

	if (port->type != PORT_UNKNOWN) {
		unsigned long flags;

		mp_report_port(drv, port);

		spin_lock_irqsave(&port->lock, flags);
		port->ops->set_mctrl(port, 0);
		spin_unlock_irqrestore(&port->lock, flags);

		mp_change_pm(state, 3);
	}
}

static void mp_unconfigure_port(struct uart_driver *drv, struct sb_uart_state *state)
{
	struct sb_uart_port *port = state->port;
	struct sb_uart_info *info = state->info;

	if (info && info->tty)
		tty_hangup(info->tty);

	MP_STATE_LOCK(state);

	state->info = NULL;

	if (port->type != PORT_UNKNOWN)
		port->ops->release_port(port);

	port->type = PORT_UNKNOWN;

	if (info) {
		tasklet_kill(&info->tlet);
		kfree(info);
	}

	MP_STATE_UNLOCK(state);
}
static struct tty_operations mp_ops = {
	.open		= mp_open,
	.close		= mp_close,
	.write		= mp_write,
	.put_char	= mp_put_char,
	.flush_chars	= mp_put_chars,
	.write_room	= mp_write_room,
	.chars_in_buffer= mp_chars_in_buffer,
	.flush_buffer	= mp_flush_buffer,
	.ioctl		= mp_ioctl,
	.throttle	= mp_throttle,
	.unthrottle	= mp_unthrottle,
	.send_xchar	= mp_send_xchar,
	.set_termios	= mp_set_termios,
	.stop		= mp_stop,
	.start		= mp_start,
	.hangup		= mp_hangup,
	.break_ctl	= mp_break_ctl,
	.wait_until_sent= mp_wait_until_sent,
#ifdef CONFIG_PROC_FS
	.proc_fops	= NULL,
#endif
	.tiocmget	= mp_tiocmget,
	.tiocmset	= mp_tiocmset,
};

static int mp_register_driver(struct uart_driver *drv)
{
	struct tty_driver *normal = NULL;
	int i, retval;

	drv->state = kmalloc(sizeof(struct sb_uart_state) * drv->nr, GFP_KERNEL);
	retval = -ENOMEM;
	if (!drv->state)
	{
		printk("SB PCI Error: Kernel memory allocation error!\n");
		goto out;
	}
	memset(drv->state, 0, sizeof(struct sb_uart_state) * drv->nr);

	normal = alloc_tty_driver(drv->nr);
	if (!normal)
	{
		printk("SB PCI Error: tty allocation error!\n");
		goto out;
	}

	drv->tty_driver = normal;

	normal->owner           = drv->owner;
	normal->magic		= TTY_DRIVER_MAGIC;
	normal->driver_name     = drv->driver_name;
	normal->name		= drv->dev_name;
	normal->major		= drv->major;
	normal->minor_start	= drv->minor;

	normal->num		= MAX_MP_PORT ; 

	normal->type		= TTY_DRIVER_TYPE_SERIAL;
	normal->subtype		= SERIAL_TYPE_NORMAL;
	normal->init_termios	= tty_std_termios;
	normal->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	normal->flags		= TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	normal->driver_state    = drv;

	tty_set_operations(normal, &mp_ops);

for (i = 0; i < drv->nr; i++) {
	struct sb_uart_state *state = drv->state + i;

	state->close_delay     = 500;   
	state->closing_wait    = 30000; 

	mutex_init(&state->mutex);
	}

	retval = tty_register_driver(normal);
out:
	if (retval < 0) {
		printk("Register tty driver Fail!\n");
		put_tty_driver(normal);
		kfree(drv->state);
	}

	return retval;
}

void mp_unregister_driver(struct uart_driver *drv)
{
    struct tty_driver *normal = NULL;

    normal = drv->tty_driver;

    if (!normal)
    {
        return;
    }

    tty_unregister_driver(normal);
    put_tty_driver(normal);
    drv->tty_driver = NULL;


    kfree(drv->state);

}

static int mp_add_one_port(struct uart_driver *drv, struct sb_uart_port *port)
{
	struct sb_uart_state *state;
	int ret = 0;


	if (port->line >= drv->nr)
		return -EINVAL;

	state = drv->state + port->line;

	MP_MUTEX_LOCK(mp_mutex);
	if (state->port) {
		ret = -EINVAL;
		goto out;
	}

	state->port = port;

	spin_lock_init(&port->lock);
	port->cons = drv->cons;
	port->info = state->info;

	mp_configure_port(drv, state, port);

	tty_register_device(drv->tty_driver, port->line, port->dev);

out:
	MP_MUTEX_UNLOCK(mp_mutex);


	return ret;
}

static int mp_remove_one_port(struct uart_driver *drv, struct sb_uart_port *port)
{
	struct sb_uart_state *state = drv->state + port->line;

	if (state->port != port)
		printk(KERN_ALERT "Removing wrong port: %p != %p\n",
				state->port, port);

	MP_MUTEX_LOCK(mp_mutex);

	tty_unregister_device(drv->tty_driver, port->line);

	mp_unconfigure_port(drv, state);
	state->port = NULL;
	MP_MUTEX_UNLOCK(mp_mutex);

	return 0;
}

static void autoconfig(struct mp_port *mtpt, unsigned int probeflags)
{
	unsigned char status1, scratch, scratch2, scratch3;
	unsigned char save_lcr, save_mcr;
	unsigned long flags;

	unsigned char u_type;
	unsigned char b_ret = 0;

	if (!mtpt->port.iobase && !mtpt->port.mapbase && !mtpt->port.membase)
		return;

	DEBUG_AUTOCONF("ttyMP%d: autoconf (0x%04x, 0x%p): ",
			mtpt->port.line, mtpt->port.iobase, mtpt->port.membase);

	spin_lock_irqsave(&mtpt->port.lock, flags);

	if (!(mtpt->port.flags & UPF_BUGGY_UART)) {
		scratch = serial_inp(mtpt, UART_IER);
		serial_outp(mtpt, UART_IER, 0);
#ifdef __i386__
		outb(0xff, 0x080);
#endif
		scratch2 = serial_inp(mtpt, UART_IER) & 0x0f;
		serial_outp(mtpt, UART_IER, 0x0F);
#ifdef __i386__
		outb(0, 0x080);
#endif
		scratch3 = serial_inp(mtpt, UART_IER) & 0x0F;
		serial_outp(mtpt, UART_IER, scratch);
		if (scratch2 != 0 || scratch3 != 0x0F) {
			DEBUG_AUTOCONF("IER test failed (%02x, %02x) ",
					scratch2, scratch3);
			goto out;
		}
	}

	save_mcr = serial_in(mtpt, UART_MCR);
	save_lcr = serial_in(mtpt, UART_LCR);

	if (!(mtpt->port.flags & UPF_SKIP_TEST)) {
		serial_outp(mtpt, UART_MCR, UART_MCR_LOOP | 0x0A);
		status1 = serial_inp(mtpt, UART_MSR) & 0xF0;
		serial_outp(mtpt, UART_MCR, save_mcr);
		if (status1 != 0x90) {
			DEBUG_AUTOCONF("LOOP test failed (%02x) ",
					status1);
			goto out;
		}
	}

	serial_outp(mtpt, UART_LCR, 0xBF);
	serial_outp(mtpt, UART_EFR, 0);
	serial_outp(mtpt, UART_LCR, 0);

	serial_outp(mtpt, UART_FCR, UART_FCR_ENABLE_FIFO);
	scratch = serial_in(mtpt, UART_IIR) >> 6;

	DEBUG_AUTOCONF("iir=%d ", scratch);
	if(mtpt->device->nr_ports >= 8)
		b_ret = read_option_register(mtpt,(MP_OPTR_DIR0 + ((mtpt->port.line)/8)));
	else	
		b_ret = read_option_register(mtpt,MP_OPTR_DIR0);
	u_type = (b_ret & 0xf0) >> 4;
	if(mtpt->port.type == PORT_UNKNOWN )
	{
		switch (u_type)
		{
			case DIR_UART_16C550:
				mtpt->port.type = PORT_16C55X;
				break;
			case DIR_UART_16C1050:
				mtpt->port.type = PORT_16C105X;
				break;
			case DIR_UART_16C1050A:
				if (mtpt->port.line < 2)
				{
					mtpt->port.type = PORT_16C105XA;
				}
				else
				{
					if (mtpt->device->device_id & 0x50)
					{
						mtpt->port.type = PORT_16C55X;
					}
					else
					{
						mtpt->port.type = PORT_16C105X;
					}
				}
				break;
			default:	
				mtpt->port.type = PORT_UNKNOWN;
				break;
		}
	}

	if(mtpt->port.type == PORT_UNKNOWN )
	{
printk("unknow2\n");
		switch (scratch) {
			case 0:
			case 1:
				mtpt->port.type = PORT_UNKNOWN;
				break;
			case 2:
			case 3:
				mtpt->port.type = PORT_16C55X;
				break;
		}
	}

	serial_outp(mtpt, UART_LCR, save_lcr);

	mtpt->port.fifosize = uart_config[mtpt->port.type].dfl_xmit_fifo_size;
	mtpt->capabilities = uart_config[mtpt->port.type].flags;

	if (mtpt->port.type == PORT_UNKNOWN)
		goto out;
	serial_outp(mtpt, UART_MCR, save_mcr);
	serial_outp(mtpt, UART_FCR, (UART_FCR_ENABLE_FIFO |
				UART_FCR_CLEAR_RCVR |
				UART_FCR_CLEAR_XMIT));
	serial_outp(mtpt, UART_FCR, 0);
	(void)serial_in(mtpt, UART_RX);
	serial_outp(mtpt, UART_IER, 0);

out:
	spin_unlock_irqrestore(&mtpt->port.lock, flags);
	DEBUG_AUTOCONF("type=%s\n", uart_config[mtpt->port.type].name);
}

static void autoconfig_irq(struct mp_port *mtpt)
{
	unsigned char save_mcr, save_ier;
	unsigned long irqs;
	int irq;

	/* forget possible initially masked and pending IRQ */
	probe_irq_off(probe_irq_on());
	save_mcr = serial_inp(mtpt, UART_MCR);
	save_ier = serial_inp(mtpt, UART_IER);
	serial_outp(mtpt, UART_MCR, UART_MCR_OUT1 | UART_MCR_OUT2);

	irqs = probe_irq_on();
	serial_outp(mtpt, UART_MCR, 0);
	serial_outp(mtpt, UART_MCR,
		UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);

	serial_outp(mtpt, UART_IER, 0x0f);    /* enable all intrs */
	(void)serial_inp(mtpt, UART_LSR);
	(void)serial_inp(mtpt, UART_RX);
	(void)serial_inp(mtpt, UART_IIR);
	(void)serial_inp(mtpt, UART_MSR);
	serial_outp(mtpt, UART_TX, 0xFF);
	irq = probe_irq_off(irqs);

	serial_outp(mtpt, UART_MCR, save_mcr);
	serial_outp(mtpt, UART_IER, save_ier);

	mtpt->port.irq = (irq > 0) ? irq : 0;
}

static void multi_stop_tx(struct sb_uart_port *port)
{
	struct mp_port *mtpt = (struct mp_port *)port;

	if (mtpt->ier & UART_IER_THRI) {
		mtpt->ier &= ~UART_IER_THRI;
		serial_out(mtpt, UART_IER, mtpt->ier);
	}

	tasklet_schedule(&port->info->tlet);
}

static void multi_start_tx(struct sb_uart_port *port)
{
	struct mp_port *mtpt = (struct mp_port *)port;

	if (!(mtpt->ier & UART_IER_THRI)) {
		mtpt->ier |= UART_IER_THRI;
		serial_out(mtpt, UART_IER, mtpt->ier);
	}
}

static void multi_stop_rx(struct sb_uart_port *port)
{
	struct mp_port *mtpt = (struct mp_port *)port;

	mtpt->ier &= ~UART_IER_RLSI;
	mtpt->port.read_status_mask &= ~UART_LSR_DR;
	serial_out(mtpt, UART_IER, mtpt->ier);
}

static void multi_enable_ms(struct sb_uart_port *port)
{
	struct mp_port *mtpt = (struct mp_port *)port;

	mtpt->ier |= UART_IER_MSI;
	serial_out(mtpt, UART_IER, mtpt->ier);
}


static _INLINE_ void receive_chars(struct mp_port *mtpt, int *status )
{
	struct tty_struct *tty = mtpt->port.info->tty;
	unsigned char lsr = *status;
	int max_count = 256;
	unsigned char ch;
	char flag;

	//lsr &= mtpt->port.read_status_mask;

	do {
		if ((lsr & UART_LSR_PE) && (mtpt->port.mdmode & MDMODE_ENABLE))
		{
			ch = serial_inp(mtpt, UART_RX);
		}
		else if (lsr & UART_LSR_SPECIAL) 
		{
			flag = 0;
			ch = serial_inp(mtpt, UART_RX);

			if (lsr & UART_LSR_BI) 
			{

				mtpt->port.icount.brk++;
				flag = TTY_BREAK;

				if (sb_uart_handle_break(&mtpt->port))
					goto ignore_char;
			} 
			if (lsr & UART_LSR_PE)
			{
				mtpt->port.icount.parity++;
				flag = TTY_PARITY;
			}
			if (lsr & UART_LSR_FE)
			{
				mtpt->port.icount.frame++;
				flag = TTY_FRAME;
			}
			if (lsr & UART_LSR_OE)
			{
				mtpt->port.icount.overrun++;
				flag = TTY_OVERRUN;
			}
			tty_insert_flip_char(tty, ch, flag);
		}
		else
		{
			ch = serial_inp(mtpt, UART_RX);
			tty_insert_flip_char(tty, ch, 0);
		}
ignore_char:
		lsr = serial_inp(mtpt, UART_LSR);
	} while ((lsr & UART_LSR_DR) && (max_count-- > 0));

	tty_flip_buffer_push(tty);
}




static _INLINE_ void transmit_chars(struct mp_port *mtpt)
{
	struct circ_buf *xmit = &mtpt->port.info->xmit;
	int count;

	if (mtpt->port.x_char) {
		serial_outp(mtpt, UART_TX, mtpt->port.x_char);
		mtpt->port.icount.tx++;
		mtpt->port.x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&mtpt->port)) {
		multi_stop_tx(&mtpt->port);
		return;
	}

	count = uart_circ_chars_pending(xmit);

	if(count > mtpt->port.fifosize)
	{
		count = mtpt->port.fifosize;
	}

	printk("[%d] mdmode: %x\n", mtpt->port.line, mtpt->port.mdmode);
	do {
#if 0
		/* check multi-drop mode */
		if ((mtpt->port.mdmode & (MDMODE_ENABLE | MDMODE_ADDR)) == (MDMODE_ENABLE | MDMODE_ADDR))
		{
			printk("send address\n");
			/* send multi-drop address */
			serial_out(mtpt, UART_SCR, xmit->buf[xmit->tail]);
		}
		else
#endif
		{
			serial_out(mtpt, UART_TX, xmit->buf[xmit->tail]);
		}
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		mtpt->port.icount.tx++;
	} while (--count > 0);
}



static _INLINE_ void check_modem_status(struct mp_port *mtpt)
{
	int status;

	status = serial_in(mtpt, UART_MSR);

	if ((status & UART_MSR_ANY_DELTA) == 0)
		return;

	if (status & UART_MSR_TERI)
		mtpt->port.icount.rng++;
	if (status & UART_MSR_DDSR)
		mtpt->port.icount.dsr++;
	if (status & UART_MSR_DDCD)
		sb_uart_handle_dcd_change(&mtpt->port, status & UART_MSR_DCD);
	if (status & UART_MSR_DCTS)
		sb_uart_handle_cts_change(&mtpt->port, status & UART_MSR_CTS);

	wake_up_interruptible(&mtpt->port.info->delta_msr_wait);
}

static inline void multi_handle_port(struct mp_port *mtpt)
{
	unsigned int status = serial_inp(mtpt, UART_LSR);

	//printk("lsr: %x\n", status);

	if ((status & UART_LSR_DR) || (status & UART_LSR_SPECIAL))
		receive_chars(mtpt, &status);
	check_modem_status(mtpt);
	if (status & UART_LSR_THRE)
	{
		if ((mtpt->port.type == PORT_16C105X)
			|| (mtpt->port.type == PORT_16C105XA))
			transmit_chars(mtpt);
		else
		{
			if (mtpt->interface >= RS485NE)
				uart_set_mctrl(&mtpt->port, TIOCM_RTS);
			
			transmit_chars(mtpt);


			if (mtpt->interface >= RS485NE)
			{
				while((status=serial_in(mtpt,UART_LSR) &0x60)!=0x60);
				uart_clear_mctrl(&mtpt->port, TIOCM_RTS);
			}
		}
	}
}



static irqreturn_t multi_interrupt(int irq, void *dev_id)
{
	struct irq_info *iinfo = dev_id;
	struct list_head *lhead, *end = NULL;
	int pass_counter = 0;


	spin_lock(&iinfo->lock);

	lhead = iinfo->head;
	do {
		struct mp_port *mtpt;
		unsigned int iir;

		mtpt = list_entry(lhead, struct mp_port, list);
		
		iir = serial_in(mtpt, UART_IIR);
		printk("interrupt! port %d, iir 0x%x\n", mtpt->port.line, iir); //wlee
		if (!(iir & UART_IIR_NO_INT)) 
		{
			printk("interrupt handle\n");
			spin_lock(&mtpt->port.lock);
			multi_handle_port(mtpt);
			spin_unlock(&mtpt->port.lock);

			end = NULL;
		} else if (end == NULL)
			end = lhead;

		lhead = lhead->next;
		if (lhead == iinfo->head && pass_counter++ > PASS_LIMIT) 
		{
			printk(KERN_ERR "multi: too much work for "
					"irq%d\n", irq);
			printk( "multi: too much work for "
					"irq%d\n", irq);
			break;
		}
	} while (lhead != end);

	spin_unlock(&iinfo->lock);


        return IRQ_HANDLED;
}

static void serial_do_unlink(struct irq_info *i, struct mp_port *mtpt)
{
	spin_lock_irq(&i->lock);

	if (!list_empty(i->head)) {
		if (i->head == &mtpt->list)
			i->head = i->head->next;
		list_del(&mtpt->list);
	} else {
		i->head = NULL;
	}

	spin_unlock_irq(&i->lock);
}

static int serial_link_irq_chain(struct mp_port *mtpt)
{
	struct irq_info *i = irq_lists + mtpt->port.irq;
	int ret, irq_flags = mtpt->port.flags & UPF_SHARE_IRQ ? IRQF_SHARED : 0;
	spin_lock_irq(&i->lock);

	if (i->head) {
		list_add(&mtpt->list, i->head);
		spin_unlock_irq(&i->lock);

		ret = 0;
	} else {
		INIT_LIST_HEAD(&mtpt->list);
		i->head = &mtpt->list;
		spin_unlock_irq(&i->lock);

		ret = request_irq(mtpt->port.irq, multi_interrupt,
				irq_flags, "serial", i);
		if (ret < 0)
			serial_do_unlink(i, mtpt);
	}

	return ret;
}




static void serial_unlink_irq_chain(struct mp_port *mtpt)
{
	struct irq_info *i = irq_lists + mtpt->port.irq;

	if (list_empty(i->head))
	{
		free_irq(mtpt->port.irq, i);
	}
	serial_do_unlink(i, mtpt);
}

static void multi_timeout(unsigned long data)
{
	struct mp_port *mtpt = (struct mp_port *)data;


	spin_lock(&mtpt->port.lock);
	multi_handle_port(mtpt);
	spin_unlock(&mtpt->port.lock);

	mod_timer(&mtpt->timer, jiffies+1 );
}

static unsigned int multi_tx_empty(struct sb_uart_port *port)
{
	struct mp_port *mtpt = (struct mp_port *)port;
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&mtpt->port.lock, flags);
	ret = serial_in(mtpt, UART_LSR) & UART_LSR_TEMT ? TIOCSER_TEMT : 0;
	spin_unlock_irqrestore(&mtpt->port.lock, flags);

	return ret;
}


static unsigned int multi_get_mctrl(struct sb_uart_port *port)
{
	struct mp_port *mtpt = (struct mp_port *)port;
	unsigned char status;
	unsigned int ret;

	status = serial_in(mtpt, UART_MSR);

	ret = 0;
	if (status & UART_MSR_DCD)
		ret |= TIOCM_CAR;
	if (status & UART_MSR_RI)
		ret |= TIOCM_RNG;
	if (status & UART_MSR_DSR)
		ret |= TIOCM_DSR;
	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;
	return ret;
}

static void multi_set_mctrl(struct sb_uart_port *port, unsigned int mctrl)
{
	struct mp_port *mtpt = (struct mp_port *)port;
	unsigned char mcr = 0;

	mctrl &= 0xff;

	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_OUT1)
		mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;


	serial_out(mtpt, UART_MCR, mcr);
}


static void multi_break_ctl(struct sb_uart_port *port, int break_state)
{
	struct mp_port *mtpt = (struct mp_port *)port;
	unsigned long flags;

	spin_lock_irqsave(&mtpt->port.lock, flags);
	if (break_state == -1)
		mtpt->lcr |= UART_LCR_SBC;
	else
		mtpt->lcr &= ~UART_LCR_SBC;
	serial_out(mtpt, UART_LCR, mtpt->lcr);
	spin_unlock_irqrestore(&mtpt->port.lock, flags);
}



static int multi_startup(struct sb_uart_port *port)
{
	struct mp_port *mtpt = (struct mp_port *)port;
	unsigned long flags;
	int retval;

	mtpt->capabilities = uart_config[mtpt->port.type].flags;
	mtpt->mcr = 0;

	if (mtpt->capabilities & UART_CLEAR_FIFO) {
		serial_outp(mtpt, UART_FCR, UART_FCR_ENABLE_FIFO);
		serial_outp(mtpt, UART_FCR, UART_FCR_ENABLE_FIFO |
				UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
		serial_outp(mtpt, UART_FCR, 0);
	}

	(void) serial_inp(mtpt, UART_LSR);
	(void) serial_inp(mtpt, UART_RX);
	(void) serial_inp(mtpt, UART_IIR);
	(void) serial_inp(mtpt, UART_MSR);
	//test-wlee 9-bit disable
	serial_outp(mtpt, UART_MSR, 0);


	if (!(mtpt->port.flags & UPF_BUGGY_UART) &&
			(serial_inp(mtpt, UART_LSR) == 0xff)) {
		printk("ttyS%d: LSR safety check engaged!\n", mtpt->port.line);
		//return -ENODEV;
	}

	if ((!is_real_interrupt(mtpt->port.irq)) || (mtpt->poll_type==TYPE_POLL)) {
		unsigned int timeout = mtpt->port.timeout;

		timeout = timeout > 6 ? (timeout / 2 - 2) : 1;

		mtpt->timer.data = (unsigned long)mtpt;
		mod_timer(&mtpt->timer, jiffies + timeout);
	} 
	else 
	{
		retval = serial_link_irq_chain(mtpt);
		if (retval)
			return retval;
	}

	serial_outp(mtpt, UART_LCR, UART_LCR_WLEN8);

	spin_lock_irqsave(&mtpt->port.lock, flags);
	if ((is_real_interrupt(mtpt->port.irq))||(mtpt->poll_type==TYPE_INTERRUPT))
		mtpt->port.mctrl |= TIOCM_OUT2;

	multi_set_mctrl(&mtpt->port, mtpt->port.mctrl);
	spin_unlock_irqrestore(&mtpt->port.lock, flags);

	
	mtpt->ier = UART_IER_RLSI | UART_IER_RDI;
	serial_outp(mtpt, UART_IER, mtpt->ier);

	(void) serial_inp(mtpt, UART_LSR);
	(void) serial_inp(mtpt, UART_RX);
	(void) serial_inp(mtpt, UART_IIR);
	(void) serial_inp(mtpt, UART_MSR);

	return 0;
}



static void multi_shutdown(struct sb_uart_port *port)
{
	struct mp_port *mtpt = (struct mp_port *)port;
	unsigned long flags;


	mtpt->ier = 0;
	serial_outp(mtpt, UART_IER, 0);

	spin_lock_irqsave(&mtpt->port.lock, flags);
	mtpt->port.mctrl &= ~TIOCM_OUT2;

	multi_set_mctrl(&mtpt->port, mtpt->port.mctrl);
	spin_unlock_irqrestore(&mtpt->port.lock, flags);

	serial_out(mtpt, UART_LCR, serial_inp(mtpt, UART_LCR) & ~UART_LCR_SBC);
	serial_outp(mtpt, UART_FCR, UART_FCR_ENABLE_FIFO |
			UART_FCR_CLEAR_RCVR |
			UART_FCR_CLEAR_XMIT);
	serial_outp(mtpt, UART_FCR, 0);


	(void) serial_in(mtpt, UART_RX);

	if ((!is_real_interrupt(mtpt->port.irq))||(mtpt->poll_type==TYPE_POLL))
	{
		del_timer_sync(&mtpt->timer);
	}
	else
	{
		serial_unlink_irq_chain(mtpt);
	}
}



static unsigned int multi_get_divisor(struct sb_uart_port *port, unsigned int baud)
{
	unsigned int quot;

	if ((port->flags & UPF_MAGIC_MULTIPLIER) &&
			baud == (port->uartclk/4))
		quot = 0x8001;
	else if ((port->flags & UPF_MAGIC_MULTIPLIER) &&
			baud == (port->uartclk/8))
		quot = 0x8002;
	else
		quot = sb_uart_get_divisor(port, baud);

	return quot;
}




static void multi_set_termios(struct sb_uart_port *port, struct MP_TERMIOS *termios, struct MP_TERMIOS *old)
{
	struct mp_port *mtpt = (struct mp_port *)port;
	unsigned char cval, fcr = 0;
	unsigned long flags;
	unsigned int baud, quot;

	switch (termios->c_cflag & CSIZE) {
		case CS5:
			cval = 0x00;
			break;
		case CS6:
			cval = 0x01;
			break;
		case CS7:
			cval = 0x02;
			break;
		default:
		case CS8:
			cval = 0x03;
			break;
	}

	if (termios->c_cflag & CSTOPB)
		cval |= 0x04;
	if (termios->c_cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(termios->c_cflag & PARODD))
		cval |= UART_LCR_EPAR;

#ifdef CMSPAR
	if (termios->c_cflag & CMSPAR)
		cval |= UART_LCR_SPAR;
#endif

	baud = sb_uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
	quot = multi_get_divisor(port, baud);

	if (mtpt->capabilities & UART_USE_FIFO) {
		//if (baud < 2400)
		//	fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;
		//else
		//	fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_8;

		//	fcr = UART_FCR_ENABLE_FIFO | 0x90;
			fcr = fcr_arr[mtpt->port.line];
	}

	spin_lock_irqsave(&mtpt->port.lock, flags);

	sb_uart_update_timeout(port, termios->c_cflag, baud);

	mtpt->port.read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (termios->c_iflag & INPCK)
		mtpt->port.read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		mtpt->port.read_status_mask |= UART_LSR_BI;

	mtpt->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		mtpt->port.ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		mtpt->port.ignore_status_mask |= UART_LSR_BI;
		if (termios->c_iflag & IGNPAR)
			mtpt->port.ignore_status_mask |= UART_LSR_OE;
	}

	if ((termios->c_cflag & CREAD) == 0)
		mtpt->port.ignore_status_mask |= UART_LSR_DR;

	mtpt->ier &= ~UART_IER_MSI;
	if (UART_ENABLE_MS(&mtpt->port, termios->c_cflag))
		mtpt->ier |= UART_IER_MSI;

	serial_out(mtpt, UART_IER, mtpt->ier);

	if (mtpt->capabilities & UART_STARTECH) {
		serial_outp(mtpt, UART_LCR, 0xBF);
		serial_outp(mtpt, UART_EFR,
				termios->c_cflag & CRTSCTS ? UART_EFR_CTS :0);
	}

	serial_outp(mtpt, UART_LCR, cval | UART_LCR_DLAB);/* set DLAB */

	serial_outp(mtpt, UART_DLL, quot & 0xff);     /* LS of divisor */
	serial_outp(mtpt, UART_DLM, quot >> 8);       /* MS of divisor */

	serial_outp(mtpt, UART_LCR, cval);        /* reset DLAB */
	mtpt->lcr = cval;                 /* Save LCR */

	if (fcr & UART_FCR_ENABLE_FIFO) {
		/* emulated UARTs (Lucent Venus 167x) need two steps */
		serial_outp(mtpt, UART_FCR, UART_FCR_ENABLE_FIFO);
	}

	serial_outp(mtpt, UART_FCR, fcr);     /* set fcr */


	if ((mtpt->port.type == PORT_16C105X)
		|| (mtpt->port.type == PORT_16C105XA))
	{
		if(deep[mtpt->port.line]!=0)
			set_deep_fifo(port, ENABLE);

		if (mtpt->interface != RS232)
			set_auto_rts(port,mtpt->interface);

	}
	else
	{
		if (mtpt->interface >= RS485NE)
		{
			uart_clear_mctrl(&mtpt->port, TIOCM_RTS);
		}
	}

	if(mtpt->device->device_id == PCI_DEVICE_ID_MP4M)
	{
		SendATCommand(mtpt);
		printk("SendATCommand\n");
	}	
	multi_set_mctrl(&mtpt->port, mtpt->port.mctrl);
	spin_unlock_irqrestore(&mtpt->port.lock, flags);
}

static void multi_pm(struct sb_uart_port *port, unsigned int state, unsigned int oldstate)
{
	struct mp_port *mtpt = (struct mp_port *)port;
	if (state) {
		if (mtpt->capabilities & UART_STARTECH) {
			serial_outp(mtpt, UART_LCR, 0xBF);
			serial_outp(mtpt, UART_EFR, UART_EFR_ECB);
			serial_outp(mtpt, UART_LCR, 0);
			serial_outp(mtpt, UART_IER, UART_IERX_SLEEP);
			serial_outp(mtpt, UART_LCR, 0xBF);
			serial_outp(mtpt, UART_EFR, 0);
			serial_outp(mtpt, UART_LCR, 0);
		}

		if (mtpt->pm)
			mtpt->pm(port, state, oldstate);
	} 
	else 
	{
		if (mtpt->capabilities & UART_STARTECH) {
			serial_outp(mtpt, UART_LCR, 0xBF);
			serial_outp(mtpt, UART_EFR, UART_EFR_ECB);
			serial_outp(mtpt, UART_LCR, 0);
			serial_outp(mtpt, UART_IER, 0);
			serial_outp(mtpt, UART_LCR, 0xBF);
			serial_outp(mtpt, UART_EFR, 0);
			serial_outp(mtpt, UART_LCR, 0);
		}

		if (mtpt->pm)
			mtpt->pm(port, state, oldstate);
	}
}

static void multi_release_std_resource(struct mp_port *mtpt)
{
	unsigned int size = 8 << mtpt->port.regshift;

	switch (mtpt->port.iotype) {
		case UPIO_MEM:
			if (!mtpt->port.mapbase)
				break;

			if (mtpt->port.flags & UPF_IOREMAP) {
				iounmap(mtpt->port.membase);
				mtpt->port.membase = NULL;
			}

			release_mem_region(mtpt->port.mapbase, size);
			break;

		case UPIO_HUB6:
		case UPIO_PORT:
			release_region(mtpt->port.iobase,size);
			break;
	}
}

static void multi_release_port(struct sb_uart_port *port)
{
}

static int multi_request_port(struct sb_uart_port *port)
{
	return 0;
}

static void multi_config_port(struct sb_uart_port *port, int flags)
{
	struct mp_port *mtpt = (struct mp_port *)port;
	int probeflags = PROBE_ANY;

	if (flags & UART_CONFIG_TYPE)
		autoconfig(mtpt, probeflags);
	if (mtpt->port.type != PORT_UNKNOWN && flags & UART_CONFIG_IRQ)
		autoconfig_irq(mtpt);

	if (mtpt->port.type == PORT_UNKNOWN)
		multi_release_std_resource(mtpt);
}

static int multi_verify_port(struct sb_uart_port *port, struct serial_struct *ser)
{
	if (ser->irq >= NR_IRQS || ser->irq < 0 ||
			ser->baud_base < 9600 || ser->type < PORT_UNKNOWN ||
			ser->type == PORT_STARTECH)
		return -EINVAL;
	return 0;
}

static const char *multi_type(struct sb_uart_port *port)
{
	int type = port->type;

	if (type >= ARRAY_SIZE(uart_config))
		type = 0;
	return uart_config[type].name;
}

static struct sb_uart_ops multi_pops = {
	.tx_empty   = multi_tx_empty,
	.set_mctrl  = multi_set_mctrl,
	.get_mctrl  = multi_get_mctrl,
	.stop_tx    = multi_stop_tx,
	.start_tx   = multi_start_tx,
	.stop_rx    = multi_stop_rx,
	.enable_ms  = multi_enable_ms,
	.break_ctl  = multi_break_ctl,
	.startup    = multi_startup,
	.shutdown   = multi_shutdown,
	.set_termios    = multi_set_termios,
	.pm     	= multi_pm,
	.type       	= multi_type,
	.release_port   = multi_release_port,
	.request_port   = multi_request_port,
	.config_port    = multi_config_port,
	.verify_port    = multi_verify_port,
};

static struct uart_driver multi_reg = {
	.owner          = THIS_MODULE,
	.driver_name    = "goldel_tulip",
	.dev_name       = "ttyMP",
	.major          = SB_TTY_MP_MAJOR,
	.minor          = 0,
	.nr             = MAX_MP_PORT, 
	.cons           = NULL,
};

static void __init multi_init_ports(void)
{
	struct mp_port *mtpt;
	static int first = 1;
	int i,j,k;
	unsigned char osc;
	unsigned char b_ret = 0;
	static struct mp_device_t *sbdev; 

	if (!first)
		return;
	first = 0;

	mtpt = multi_ports; 

	for (k=0;k<NR_BOARD;k++)
	{
		sbdev = &mp_devs[k];

		for (i = 0; i < sbdev->nr_ports; i++, mtpt++) 
		{
			mtpt->device 		= sbdev;
			mtpt->port.iobase   = sbdev->uart_access_addr + 8*i;
			mtpt->port.irq      = sbdev->irq;
			if ( ((sbdev->device_id == PCI_DEVICE_ID_MP4)&&(sbdev->revision==0x91)))
				mtpt->interface_config_addr = sbdev->option_reg_addr + 0x08 + i;
			else if (sbdev->revision == 0xc0)
				mtpt->interface_config_addr = sbdev->option_reg_addr + 0x08 + (i & 0x1);
			else
				mtpt->interface_config_addr = sbdev->option_reg_addr + 0x08 + i/8;

			mtpt->option_base_addr = sbdev->option_reg_addr;

			mtpt->poll_type = sbdev->poll_type;

			mtpt->port.uartclk  = BASE_BAUD * 16;

			/* get input clock information */
			osc = inb(sbdev->option_reg_addr + MP_OPTR_DIR0 + i/8) & 0x0F;
			if (osc==0x0f)
				osc = 0;
			for(j=0;j<osc;j++)
				mtpt->port.uartclk *= 2;
			mtpt->port.flags    |= STD_COM_FLAGS | UPF_SHARE_IRQ ;
			mtpt->port.iotype   = UPIO_PORT;
			mtpt->port.ops      = &multi_pops;

			if (sbdev->revision == 0xc0)
			{
				/* for SB16C1053APCI */
				b_ret = sb1053a_get_interface(mtpt, i);
			}
			else
			{
				b_ret = read_option_register(mtpt,(MP_OPTR_IIR0 + i/8));
				printk("IIR_RET = %x\n",b_ret);
			}

			/* default to RS232 */
			mtpt->interface = RS232;
			if (IIR_RS422 == (b_ret & IIR_TYPE_MASK))
				mtpt->interface = RS422PTP;
			if (IIR_RS485 == (b_ret & IIR_TYPE_MASK))
				mtpt->interface = RS485NE;
		}
	}
}

static void __init multi_register_ports(struct uart_driver *drv)
{
	int i;

	multi_init_ports();

	for (i = 0; i < NR_PORTS; i++) {
		struct mp_port *mtpt = &multi_ports[i];

		mtpt->port.line = i;
		mtpt->port.ops = &multi_pops;
		init_timer(&mtpt->timer);
		mtpt->timer.function = multi_timeout;
		mp_add_one_port(drv, &mtpt->port);
	}
}

/**
 * pci_remap_base - remap BAR value of pci device
 *
 * PARAMETERS
 *  pcidev  - pci_dev structure address
 *  offset  - BAR offset PCI_BASE_ADDRESS_0 ~ PCI_BASE_ADDRESS_4
 *  address - address to be changed BAR value
 *  size	- size of address space 
 *
 * RETURNS
 *  If this function performs successful, it returns 0. Otherwise, It returns -1.
 */
static int pci_remap_base(struct pci_dev *pcidev, unsigned int offset, 
		unsigned int address, unsigned int size) 
{
#if 0
	struct resource *root;
	unsigned index = (offset - 0x10) >> 2;
#endif

	pci_write_config_dword(pcidev, offset, address);
#if 0
	root = pcidev->resource[index].parent;
	release_resource(&pcidev->resource[index]);
	address &= ~0x1;
	pcidev->resource[index].start = address;
	pcidev->resource[index].end	  = address + size - 1;

	if (request_resource(root, &pcidev->resource[index]) != NULL)
	{
		printk(KERN_ERR "pci remap conflict!! 0x%x\n", address);
		return (-1);
	}
#endif

	return (0);
}

static int init_mp_dev(struct pci_dev *pcidev, mppcibrd_t brd)
{
	static struct mp_device_t *sbdev = mp_devs;
	unsigned long addr = 0;
	int j;
	struct resource *ret = NULL;

	sbdev->device_id = brd.device_id;
	pci_read_config_byte(pcidev, PCI_CLASS_REVISION, &(sbdev->revision));
	sbdev->name = brd.name;
	sbdev->uart_access_addr = pcidev->resource[0].start & PCI_BASE_ADDRESS_IO_MASK;

	/* check revision. The SB16C1053APCI's option i/o address is BAR4 */
	if (sbdev->revision == 0xc0)
	{
		/* SB16C1053APCI */
		sbdev->option_reg_addr = pcidev->resource[4].start & PCI_BASE_ADDRESS_IO_MASK;
	}
	else
	{
		sbdev->option_reg_addr = pcidev->resource[1].start & PCI_BASE_ADDRESS_IO_MASK;
	}
#if 1	
	if (sbdev->revision == 0xc0)
	{
		outb(0x00, sbdev->option_reg_addr + MP_OPTR_GPOCR);
		inb(sbdev->option_reg_addr + MP_OPTR_GPOCR);
		outb(0x83, sbdev->option_reg_addr + MP_OPTR_GPOCR);
	}
#endif

	sbdev->irq = pcidev->irq;

	if ((brd.device_id & 0x0800) || !(brd.device_id &0xff00))
	{
		sbdev->poll_type = TYPE_INTERRUPT;
	}
	else
	{
		sbdev->poll_type = TYPE_POLL;
	}

	/* codes which is specific to each board*/
	switch(brd.device_id){
		case PCI_DEVICE_ID_MP1 :
		case PCIE_DEVICE_ID_MP1 :
		case PCIE_DEVICE_ID_MP1E :
		case PCIE_DEVICE_ID_GT_MP1 :
			sbdev->nr_ports = 1;
			break;
		case PCI_DEVICE_ID_MP2 :
		case PCIE_DEVICE_ID_MP2 :
		case PCIE_DEVICE_ID_GT_MP2 :
		case PCIE_DEVICE_ID_MP2B :
		case PCIE_DEVICE_ID_MP2E :
			sbdev->nr_ports = 2;

			/* serial base address remap */
			if (sbdev->revision == 0xc0)
			{
				int prev_port_addr = 0;

				pci_read_config_dword(pcidev, PCI_BASE_ADDRESS_0, &prev_port_addr);
				pci_remap_base(pcidev, PCI_BASE_ADDRESS_1, prev_port_addr + 8, 8);
			}
			break;
		case PCI_DEVICE_ID_MP4 :
		case PCI_DEVICE_ID_MP4A :
		case PCIE_DEVICE_ID_MP4 :
		case PCI_DEVICE_ID_GT_MP4 :
		case PCI_DEVICE_ID_GT_MP4A :
		case PCIE_DEVICE_ID_GT_MP4 :
		case PCI_DEVICE_ID_MP4M :
		case PCIE_DEVICE_ID_MP4B :
			sbdev->nr_ports = 4;

			if(sbdev->revision == 0x91){
				sbdev->reserved_addr[0] = pcidev->resource[0].start & PCI_BASE_ADDRESS_IO_MASK;
				outb(0x03 , sbdev->reserved_addr[0] + 0x01);
				outb(0x03 , sbdev->reserved_addr[0] + 0x02);
				outb(0x01 , sbdev->reserved_addr[0] + 0x20);
				outb(0x00 , sbdev->reserved_addr[0] + 0x21);
				request_region(sbdev->reserved_addr[0], 32, sbdev->name);
				sbdev->uart_access_addr = pcidev->resource[1].start & PCI_BASE_ADDRESS_IO_MASK;
				sbdev->option_reg_addr = pcidev->resource[2].start & PCI_BASE_ADDRESS_IO_MASK;
			}

			/* SB16C1053APCI */
			if (sbdev->revision == 0xc0)
			{
				int prev_port_addr = 0;

				pci_read_config_dword(pcidev, PCI_BASE_ADDRESS_0, &prev_port_addr);
				pci_remap_base(pcidev, PCI_BASE_ADDRESS_1, prev_port_addr + 8, 8);
				pci_remap_base(pcidev, PCI_BASE_ADDRESS_2, prev_port_addr + 16, 8);
				pci_remap_base(pcidev, PCI_BASE_ADDRESS_3, prev_port_addr + 24, 8);
			}
			break;
		case PCI_DEVICE_ID_MP6 :
		case PCI_DEVICE_ID_MP6A :
		case PCI_DEVICE_ID_GT_MP6 :
		case PCI_DEVICE_ID_GT_MP6A :
			sbdev->nr_ports = 6;

			/* SB16C1053APCI */
			if (sbdev->revision == 0xc0)
			{
				int prev_port_addr = 0;

				pci_read_config_dword(pcidev, PCI_BASE_ADDRESS_0, &prev_port_addr);
				pci_remap_base(pcidev, PCI_BASE_ADDRESS_1, prev_port_addr + 8, 8);
				pci_remap_base(pcidev, PCI_BASE_ADDRESS_2, prev_port_addr + 16, 16);
				pci_remap_base(pcidev, PCI_BASE_ADDRESS_3, prev_port_addr + 32, 16);
			}
			break;
		case PCI_DEVICE_ID_MP8 :
		case PCIE_DEVICE_ID_MP8 :
		case PCI_DEVICE_ID_GT_MP8 :
		case PCIE_DEVICE_ID_GT_MP8 :
		case PCIE_DEVICE_ID_MP8B :
			sbdev->nr_ports = 8;
			break;
		case PCI_DEVICE_ID_MP32 :
		case PCIE_DEVICE_ID_MP32 :
		case PCI_DEVICE_ID_GT_MP32 :
		case PCIE_DEVICE_ID_GT_MP32 :
			{
				int portnum_hex=0;
				portnum_hex = inb(sbdev->option_reg_addr);
				sbdev->nr_ports = ((portnum_hex/16)*10) + (portnum_hex % 16);
			}
			break;
#ifdef CONFIG_PARPORT_PC
		case PCI_DEVICE_ID_MP2S1P :
			sbdev->nr_ports = 2;

			/* SB16C1053APCI */
			if (sbdev->revision == 0xc0)
			{
				int prev_port_addr = 0;

				pci_read_config_dword(pcidev, PCI_BASE_ADDRESS_0, &prev_port_addr);
				pci_remap_base(pcidev, PCI_BASE_ADDRESS_1, prev_port_addr + 8, 8);
			}

			/* add PC compatible parallel port */
			parport_pc_probe_port(pcidev->resource[2].start, pcidev->resource[3].start, PARPORT_IRQ_NONE, PARPORT_DMA_NONE, &pcidev->dev, 0);
			break;
		case PCI_DEVICE_ID_MP1P :
			/* add PC compatible parallel port */
			parport_pc_probe_port(pcidev->resource[2].start, pcidev->resource[3].start, PARPORT_IRQ_NONE, PARPORT_DMA_NONE, &pcidev->dev, 0);
			break;
#endif
	}

	ret = request_region(sbdev->uart_access_addr, (8*sbdev->nr_ports), sbdev->name);

	if (sbdev->revision == 0xc0)
	{
		ret = request_region(sbdev->option_reg_addr, 0x40, sbdev->name);
	}
	else
	{
		ret = request_region(sbdev->option_reg_addr, 0x20, sbdev->name);
	}


	NR_BOARD++;
	NR_PORTS += sbdev->nr_ports;

	/* Enable PCI interrupt */
	addr = sbdev->option_reg_addr + MP_OPTR_IMR0;
	for(j=0; j < (sbdev->nr_ports/8)+1; j++)
	{
		if (sbdev->poll_type == TYPE_INTERRUPT)
		{
			outb(0xff,addr +j);
		}
	}
	sbdev++;

	return 0;
}

static int __init multi_init(void)
{
	int ret, i;
	struct pci_dev  *dev = NULL;

	if(fcr_count==0)
	{
		for(i=0;i<256;i++)
		{
			fcr_arr[i] = 0x01;
			
		}
	}
	if(deep_count==0)
	{
		for(i=0;i<256;i++)
		{
			deep[i] = 1;
			
		}
	}
	if(rtr_count==0)
        {
                for(i=0;i<256;i++)
                {
                        rtr[i] = 0x10;
                }
        }
	if(ttr_count==0)
        {
                for(i=0;i<256;i++)
                {
                        ttr[i] = 0x38;
                }
        }


printk("MULTI INIT\n");
	for( i=0; i< mp_nrpcibrds; i++)
	{

		while( (dev = pci_get_device(mp_pciboards[i].vendor_id, mp_pciboards[i].device_id, dev) ) )

		{
printk("FOUND~~~\n");
//	Cent OS bug fix
//			if (mp_pciboards[i].device_id & 0x0800)
			{
				int status;
	        		pci_disable_device(dev);
	        		status = pci_enable_device(dev);
            
	   		     	if (status != 0)
        			{ 
               				printk("Multiport Board Enable Fail !\n\n");
               				status = -ENXIO;
                			return status;
           			}
			}

			init_mp_dev(dev, mp_pciboards[i]);	
		}
	}

	for (i = 0; i < NR_IRQS; i++)
		spin_lock_init(&irq_lists[i].lock);

	ret = mp_register_driver(&multi_reg);

	if (ret >= 0)
		multi_register_ports(&multi_reg);

	return ret;
}

static void __exit multi_exit(void)
{
	int i;

	for (i = 0; i < NR_PORTS; i++)
		mp_remove_one_port(&multi_reg, &multi_ports[i].port);

	mp_unregister_driver(&multi_reg);
}

module_init(multi_init);
module_exit(multi_exit);

MODULE_DESCRIPTION("SystemBase Multiport PCI/PCIe CORE");
MODULE_LICENSE("GPL");
