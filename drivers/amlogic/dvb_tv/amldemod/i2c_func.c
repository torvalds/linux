#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include "aml_demod.h"
#include "demod_func.h"

// basic functions
static void sw_i2c_setscl(struct aml_demod_i2c *adap, unsigned val)
{
    u32 oe, scl;

    oe = *(volatile u32 *)(adap->scl_oe);
    oe &= ~(1<<adap->scl_bit);
    *(volatile u32 *)(adap->scl_oe) = oe;

    scl = *(volatile u32 *)(adap->scl_out);
    if(val)
        scl |= (1<<(adap->scl_bit));
    else
        scl &= ~(1<<(adap->scl_bit));
    *(volatile u32 *)(adap->scl_out) = scl;
}

static void sw_i2c_setsda(struct aml_demod_i2c *adap, unsigned val)
{
    u32 oe, sda;

    oe = *(volatile u32 *)(adap->sda_oe);
    oe &= ~(1<<adap->sda_bit);
    *(volatile u32 *)(adap->sda_oe) = oe;

    sda = *(volatile u32 *)(adap->sda_out);
    if(val)
        sda |= (1<<(adap->sda_bit));
    else
        sda &= ~(1<<(adap->sda_bit));
    *(volatile u32 *)(adap->sda_out) = sda;
}
#if 0
static int sw_i2c_getscl(struct aml_demod_i2c *adap)
{
    u32 oe, scl;

    oe = *(volatile u32 *)(adap->scl_oe);
    oe |= (1<<adap->scl_bit);
    *(volatile u32 *)(adap->scl_oe) = oe;

    scl = *(volatile u32 *)(adap->scl_in);
    scl = (scl>>adap->scl_bit)&1;

    return scl;
}
#endif

static int sw_i2c_getsda(struct aml_demod_i2c *adap)
{
    u32 oe, sda;

    oe = *(volatile u32 *)(adap->sda_oe);
    oe |= (1<<adap->sda_bit);
    *(volatile u32 *)(adap->sda_oe) = oe;

    sda = *(volatile u32 *)(adap->sda_in);
    sda = (sda>>adap->sda_bit)&1;

    return sda;
}

// set high/low with delay
static inline void sdalo(struct aml_demod_i2c *adap)
{
    sw_i2c_setsda(adap, 0);
    udelay(adap->udelay);
}

static inline void sdahi(struct aml_demod_i2c *adap)
{
    sw_i2c_setsda(adap, 1);
    udelay(adap->udelay);
}

static inline void scllo(struct aml_demod_i2c *adap)
{
    sw_i2c_setscl(adap, 0);
    udelay(adap->udelay);
}

static inline void sclhi(struct aml_demod_i2c *adap)
{
    sw_i2c_setscl(adap, 1);
    udelay(adap->udelay);
}

// i2c patterns
static void i2c_start(struct aml_demod_i2c *adap)
{
    sdahi(adap);
    sclhi(adap);
    sdalo(adap);
    scllo(adap);
}

static void i2c_stop(struct aml_demod_i2c *adap)
{
    sdalo(adap);
    sclhi(adap);
    sdahi(adap);
}

static int i2c_outb(struct aml_demod_i2c *adap, unsigned char c)
{
    int i, sb, ack;

    /* assert: scl is low */
    for (i=7; i>=0; i--) {
        sb = c>>i&1;

        sw_i2c_setsda(adap, sb);
        udelay(adap->udelay);
        sclhi(adap);
        scllo(adap);
    }
    sdahi(adap);
    sclhi(adap);

    ack = !sw_i2c_getsda(adap); /* ack: sda low -> success */
    scllo(adap);

    if (adap->debug) printk("i2c: out 0x%02x  ack %d\n", c, ack);

    return ack;
}

static int i2c_inb(struct aml_demod_i2c *adap)
{
    int i;
    unsigned char indata = 0;

    /* assert: scl is low */
    sdahi(adap);
    for (i=0; i<8; i++) {
        sclhi(adap);
        indata *= 2;
        if (sw_i2c_getsda(adap))
            indata |= 0x01;
        scllo(adap);
    }
    /* assert: scl is low */

    if (adap->debug) printk("i2c: in  0x%02x\n", indata);

    return indata;
}

#if 0
static int aml_i2c_sw_test_bus(struct aml_demod_i2c *adap, char *name)
{
    int scl, sda;

    sda = sw_i2c_getsda(adap);
    scl = sw_i2c_getscl(adap);
    if (!scl || !sda) {
        printk("%s: bus seems to be busy\n", name);
        goto bailout;
    }

    sdalo(adap);
    sda = sw_i2c_getsda(adap);
    scl = sw_i2c_getscl(adap);
    if (sda) {
        printk("%s: SDA stuck high!\n", name);
        goto bailout;
    }
    if (!scl) {
        printk("%s: SCL unexpected low while pulling SDA low!\n", name);
        goto bailout;
    }

    sdahi(adap);
    sda = sw_i2c_getsda(adap);
    scl = sw_i2c_getscl(adap);
    if (!sda) {
        printk("%s: SDA stuck low!\n", name);
        goto bailout;
    }
    if (!scl) {
        printk("%s: SCL unexpected low while pulling SDA high!\n", name);
        goto bailout;
    }

    scllo(adap);
    sda = sw_i2c_getsda(adap);
    scl = sw_i2c_getscl(adap);
    if (scl) {
        printk("%s: SCL stuck high!\n", name);
        goto bailout;
    }
    if (!sda) {
        printk("%s: SDA unexpected low while pulling SCL low!\n", name);
        goto bailout;
    }

    sclhi(adap);
    sda = sw_i2c_getsda(adap);
    scl = sw_i2c_getscl(adap);
    if (!scl) {
        printk("%s: SCL stuck low!\n", name);
        goto bailout;
    }
    if (!sda) {
        printk("%s: SDA unexpected low while pulling SCL high!\n", name);
        goto bailout;
    }
    if (adap->debug) printk("%s: Test OK\n", name);
    return 0;

bailout:
    sdahi(adap);
    sclhi(adap);
    return -ENODEV;
}
#endif

static int try_address(struct aml_demod_i2c *adap,
		       unsigned char addr, int retries)
{
    int i, ret = 0;

    for (i=0; i<=retries; i++) {
        ret = i2c_outb(adap, addr);
        if (ret == 1 || i == retries)
            break;
        if (adap->debug) printk("emitting stop condition\n");
        i2c_stop(adap);
        udelay(adap->udelay);
        if (adap->debug) printk("emitting start condition\n");
        i2c_start(adap);
    }
    if (i && ret && adap->debug)
	printk("Used %d tries to %s client at 0x%02x: %s\n",
	       i + 1, addr & 1 ? "read from" : "write to", addr >> 1,
	       ret == 1 ? "success" : "failed, timeout?");
    return ret;
}

static int sendbytes(struct aml_demod_i2c *adap, struct i2c_msg *msg)
{
    unsigned char *temp = msg->buf;
    int count = msg->len;
    unsigned short nak_ok = msg->flags & I2C_M_IGNORE_NAK;
    int retval;
    int wrcount = 0;

    while (count > 0) {
        retval = i2c_outb(adap, *temp);

        /* OK/ACK; or ignored NAK */
        if ((retval > 0) || (nak_ok && (retval == 0))) {
            count--;
            temp++;
            wrcount++;
        } else if (retval == 0) {
            printk("Error: sendbytes: NAK bailout.\n");
            return -EIO;
        } else {
            printk("Error: sendbytes: error %d\n", retval);
            return retval;
        }
    }
    return wrcount;
}

static void acknak(struct aml_demod_i2c *adap, int is_ack)
{
    /* assert: sda is high */
    if (is_ack) sw_i2c_setsda(adap, 0);
    udelay(adap->udelay);
    sclhi(adap);
    scllo(adap);
}

static int readbytes(struct aml_demod_i2c *adap, struct i2c_msg *msg)
{
    int inval;
    int rdcount = 0;    /* counts bytes read */
    unsigned char *temp = msg->buf;
    int count = msg->len;
    const unsigned flags = msg->flags;

    while (count > 0) {
        inval = i2c_inb(adap);
        if (inval >= 0) {
            *temp = inval;
            rdcount++;
        } else {   /* read timed out */
            break;
        }

        temp++;
        count--;

        /* Some SMBus transactions require that we receive the
           transaction length as the first read byte. */
        if (rdcount == 1 && (flags & I2C_M_RECV_LEN)) {
            if (inval <= 0 || inval > I2C_SMBUS_BLOCK_MAX) {
                if (!(flags & I2C_M_NO_RD_ACK)) acknak(adap, 0);
                printk("Error: readbytes: invalid block length (%d)\n", inval);
                return -EREMOTEIO;
            }
            /* The original count value accounts for the extra
               bytes, that is, either 1 for a regular transaction,
               or 2 for a PEC transaction. */
            count += inval;
            msg->len += inval;
        }

        if (!(flags & I2C_M_NO_RD_ACK)) acknak(adap, count);
    }
    return rdcount;
}

static int bit_doAddress(struct aml_demod_i2c *adap, struct i2c_msg *msg)
{
    unsigned short flags = msg->flags;
    unsigned short nak_ok = msg->flags & I2C_M_IGNORE_NAK;
    unsigned char addr;
    int ret, retries;

    retries = nak_ok ? 0 : adap->retries;

    if (flags & I2C_M_TEN) {
        /* a ten bit address */
        addr = 0xf0 | ((msg->addr >> 7) & 0x03);
        if (adap->debug) printk("i2c addr0: %d\n", addr);
        /* try extended address code...*/
        ret = try_address(adap, addr, retries);
        if ((ret != 1) && !nak_ok)  {
            printk("Error: died at extended address code\n");
            return -EREMOTEIO;
        }
        /* the remaining 8 bit address */
        ret = i2c_outb(adap, msg->addr & 0x7f);
        if ((ret != 1) && !nak_ok) {
            /* the chip did not ack / xmission error occurred */
            printk("Error: died at 2nd address code\n");
            return -EREMOTEIO;
        }
        if (flags & I2C_M_RD) {
            if (adap->debug) printk("emitting repeated start condition\n");
            i2c_start(adap);
            /* okay, now switch into reading mode */
            addr |= 0x01;
            ret = try_address(adap, addr, retries);
            if ((ret != 1) && !nak_ok) {
                printk("Error: died at repeated address code\n");
                return -EREMOTEIO;
            }
        }
    }
    else { /* normal 7bit address  */
        addr = msg->addr << 1;
        if (flags & I2C_M_RD)
            addr |= 1;
        if (flags & I2C_M_REV_DIR_ADDR)
            addr ^= 1;
        ret = try_address(adap, addr, retries);
        if ((ret != 1) && !nak_ok)
            return -ENXIO;
    }

    return 0;
}

// i2c read/write function
static int aml_i2c_sw_bit_xfer(struct aml_demod_i2c *adap, struct i2c_msg msgs[], int num)
{
    struct i2c_msg *pmsg;
    int i, ret;
    unsigned short nak_ok;

    if (adap->debug) printk("emitting start condition\n");
    i2c_start(adap);
    for (i = 0; i < num; i++) {
        pmsg = &msgs[i];
        nak_ok = pmsg->flags & I2C_M_IGNORE_NAK;
        if (!(pmsg->flags & I2C_M_NOSTART)) {
            if (i) {
                if (adap->debug) printk("emitting repeated start condition\n");
                i2c_start(adap);
            }
            ret = bit_doAddress(adap, pmsg);
            if ((ret != 0) && !nak_ok) {
                printk("NAK from device addr 0x%02x msg #%d\n",
		       msgs[i].addr, i);
                goto bailout;
            }
        }
        if (pmsg->flags & I2C_M_RD) {
            /* read bytes into buffer*/
            ret = readbytes(adap, pmsg);
            if (ret >= 1 && adap->debug)
                printk("read %d byte%s\n", ret, ret == 1 ? "" : "s");
            if (ret < pmsg->len) {
                if (ret >= 0)
                    ret = -EREMOTEIO;
                goto bailout;
            }
        } else {
            /* write bytes from buffer */
            ret = sendbytes(adap, pmsg);
            if (ret >= 1 && adap->debug)
                printk("wrote %d byte%s\n", ret, ret == 1 ? "" : "s");
            if (ret < pmsg->len) {
                if (ret >= 0)
                    ret = -EREMOTEIO;
                goto bailout;
            }
        }
    }
    ret = i;

bailout:
    if (adap->debug) printk("emitting stop condition\n");
    i2c_stop(adap);

    return ret;
}

int am_demod_i2c_xfer(struct aml_demod_i2c *adap, struct i2c_msg msgs[], int num)
{
    int ret=0;

    //printk("adap->scl_oe[%x], adap->i2c_priv[%p], adap->i2c_id[%x]\n", adap->scl_oe, adap->i2c_priv, adap->i2c_id);
    if(adap->scl_oe)
    {
    	ret = aml_i2c_sw_bit_xfer(adap, msgs, num);
    }
    else
    {
    	if(adap->i2c_priv){
		//printk("msgs[%p], num[%x]\n", msgs, num);
		ret = i2c_transfer((struct i2c_adapter *)adap->i2c_priv, msgs, num);
	} else {
    		printk("i2c error, no valid i2c\n");
    	}
    }
    return ret;
}

