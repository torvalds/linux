/*
 *  rt5025_gauge.c
 *  fuel-gauge driver
 *  revision 0.1
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/android_alarm.h>
#include <linux/mfd/rt5025.h>
#include <linux/power/rt5025-gauge.h>

#define RT5025_REG_IRQ_CTL						0x50
#define RT5025_REG_IRQ_FLAG						0x51
#define RT5025_REG_VALRT_MAXTH				0x53
#define RT5025_REG_VALRT_MIN1TH				0x54
#define RT5025_REG_VALRT_MIN2TH				0x55
#define RT5025_REG_TALRT_MAXTH				0x56
#define RT5025_REG_TALRT_MINTH				0x57
#define RT5025_REG_VCELL_MSB					0x58
#define RT5025_REG_VCELL_LSB					0x59
#define RT5025_REG_INT_TEMPERATUE_MSB	0x5B
#define RT5025_REG_INT_TEMPERATUE_LSB 0x5C
#define RT5025_REG_EXT_TEMPERATUE_MSB	0x5E
#define RT5025_REG_EXT_TEMPERATUE_LSB	0x5F
#define RT5025_REG_TIMER							0x60
#define RT5025_REG_CHANNEL_MSB       	0x62
#define RT5025_REG_CHANNEL_LSB       	0x63
#define RT5025_REG_CURRENT_MSB				0x76
#define RT5025_REG_CURRENT_LSB				0x77
#define RT5025_REG_QCHGH_MSB          0x78
#define RT5025_REG_QCHGH_LSB          0x79
#define RT5025_REG_QCHGL_MSB          0x7A
#define RT5025_REG_QCHGL_LSB          0x7B
#define RT5025_REG_QDCHGH_MSB					0x7C
#define RT5025_REG_QDCHGH_LSB					0x7D
#define RT5025_REG_QDCHGL_MSB					0x7E
#define RT5025_REG_QDCHGL_LSB					0x7F

#define IRQ_CTL_BIT_TMX  (1 << 5)
#define IRQ_CTL_BIT_TMN  (1 << 4)
#define IRQ_CTL_BIT_VMX  (1 << 2)
#define IRQ_CTL_BIT_VMN1 (1 << 1)
#define IRQ_CTL_BIT_VMN2 (1 << 0)

#define IRQ_FLG_BIT_TMX  (1 << 5)
#define IRQ_FLG_BIT_TMN  (1 << 4)
#define IRQ_FLG_BIT_VMX  (1 << 2)
#define IRQ_FLG_BIT_VMN1 (1 << 1)
#define IRQ_FLG_BIT_VMN2 (1 << 0)

#define CHANNEL_H_BIT_CLRQDCHG  (1 << 7)
#define CHANNEL_H_BIT_CLRQCHG   (1 << 6)

#define CHANNEL_L_BIT_CADC_EN   (1 << 7)
#define CHANNEL_L_BIT_INTEMPCH  (1 << 6)
#define CHANNEL_L_BIT_AINCH     (1 << 2)
#define CHANNEL_L_BIT_VBATSCH   (1 << 1)
#define CHANNEL_L_BIT_VADC_EN   (1 << 0)

#define NORMAL_POLL 10  /* 10 sec */
#define SUSPEND_POLL (30*60) /* 30 min */

#define HIGH_TEMP_THRES	650
#define HIGH_TEMP_RECOVER	430
#define LOW_TEMP_THRES (-30)
#define LOW_TEMP_RECOVER 0
#define TEMP_ABNORMAL_COUNT	3

#define TALRTMAX_VALUE  0x38 //65.39'C 0x9
#define TALRTMIN_VALUE  0x11 //-18.75'C 0x17
#define VALRTMAX_VALUE  0xDC //4297mV
#define VALRTMIN1_VALUE 0xB8 //3600mV
#define VALRTMIN2_VALUE 0x99 //3000mV
#define TRLS_VALUE      55   //5'C
#define VRLS_VALUE      100  //100mV

#define IRQ_THRES_UNIT 1953

struct rt5025_gauge_chip {
  struct i2c_client *client;
  struct rt5025_power_info *info;
  struct rt5025_gauge_callbacks cb;

	struct power_supply	battery;
	
	struct delayed_work monitor_work;
	struct wake_lock monitor_wake_lock;
	struct alarm wakeup_alarm;
	
	bool	suspend_poll;
	ktime_t	last_poll;
	
  /* battery voltage */
  u16 vcell;
  /* battery current */
  s16 curr;
  /* battery current offset */
  u8 curr_offset;
  /* AIN voltage */
  u16 ain_volt;
  /* battery external temperature */
  s16 ext_temp;
  /* charge coulomb counter */
  u32 chg_cc;
  u32 chg_cc_unuse;
  /* discharge coulomb counter */
  u32 dchg_cc;
  u32 dchg_cc_unuse;
  /* battery capacity */
  u8 soc;
  u16 time_interval;
  u16 pre_gauge_timer;
    
  u8 online;
  u8 status;
  u8 health;

  /* IRQ flag */
  u8 irq_flag;
   
  /* max voltage IRQ flag */
  bool max_volt_irq;
  /* min voltage1 IRQ flag */
  bool min_volt1_irq;  
  /* min voltage2 IRQ flag */
  bool min_volt2_irq;
  /* max temperature IRQ flag */
  bool max_temp_irq;
  /* min temperature IRQ flag */
  bool min_temp_irq;
  
	u8 temp_high_cnt;
	u8 temp_low_cnt;
	u8 temp_recover_cnt;
};

struct rt5025_gauge_chip *chip;
u8 irq_thres[LAST_TYPE];

void rt5025_set_status(int status)
{
  chip->status = status; 
}

static int rt5025_read_reg(struct i2c_client *client,
				u8 reg, u8 *data, u8 len)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msgs[2];
	int ret;
	
	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 1;
	msgs[0].buf = &reg;
	msgs[0].scl_rate = 200*1000;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = data;
	msgs[1].scl_rate = 200*1000;
	
	ret = i2c_transfer(adap, msgs, 2);
	 
	return (ret == 2)? len : ret;  
}

static int rt5025_write_reg(struct i2c_client *client,
				u8 reg, u8 *data, u8 len)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	int ret;
	char *tx_buf = (char *)kmalloc(len + 1, GFP_KERNEL);
	
	if(!tx_buf)
		return -ENOMEM;
	tx_buf[0] = reg;
	memcpy(tx_buf+1, data, len);
	
	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len + 1;
	msg.buf = (char *)tx_buf;
	msg.scl_rate = 200*1000;	
	ret = i2c_transfer(adap, &msg, 1);
	kfree(tx_buf);
	return (ret == 1) ? len : ret; 
}

static void rt5025_gauge_alarm(struct alarm *alarm)
{
	wake_lock(&chip->monitor_wake_lock);
	schedule_delayed_work(&chip->monitor_work, 0);
}

static void rt5025_program_alarm(int seconds)
{
	ktime_t low_interval = ktime_set(seconds, 0);
	ktime_t slack = ktime_set(20, 0);
	ktime_t next;

	next = ktime_add(chip->last_poll, low_interval);
	alarm_start_range(&chip->wakeup_alarm, next, ktime_add(next, slack));
}

static int rt5025_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
  switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:
      val->intval = chip->status;
      break;
    case POWER_SUPPLY_PROP_HEALTH:
      val->intval = chip->health;
      break;
    case POWER_SUPPLY_PROP_PRESENT:
      val->intval = chip->online;
      break;
    case POWER_SUPPLY_PROP_TEMP:
      val->intval = chip->ext_temp;
      break;
    case POWER_SUPPLY_PROP_ONLINE:
      val->intval = 1;
      break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
      val->intval = chip->vcell * 1000; //uV
      break;
    case POWER_SUPPLY_PROP_CURRENT_NOW:
      val->intval = chip->curr * 1000; //uA
      break;
    case POWER_SUPPLY_PROP_CAPACITY:
      val->intval = chip->soc;
      if (val->intval > 100)
				val->intval = 100;
      break;
    case POWER_SUPPLY_PROP_TECHNOLOGY:
      val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

static void rt5025_get_vcell(struct i2c_client *client)
{
  u8 data[2];
	
  if (rt5025_read_reg(client, RT5025_REG_VCELL_MSB, data, 2) < 0){
    printk(KERN_ERR "%s: Failed to read Voltage\n", __func__);
  }
		
  chip->vcell = ((data[0] << 8) + data[1]) * 61 / 100;
	chip->curr_offset = (15444 * chip->vcell - 27444000) / 10000;
		
  RTINFO("[RT5025] vcell: %d, offset: %d\n", chip->vcell, chip->curr_offset);
}

static void rt5025_get_current(struct i2c_client *client)
{
  u8 data[2];
  u16 temp;
  int sign = 0;

  if (rt5025_read_reg(client, RT5025_REG_CURRENT_MSB, data, 2) < 0) {
    printk(KERN_ERR "%s: Failed to read CURRENT\n", __func__);
  }

  temp = (data[0]<<8) | data[1];
  if (data[0] & (1 << 7)) {
    sign = 1;
    temp = (((temp & 0x7FFF) * 3125) / 10 + chip->curr_offset) / 1000;
  }else
		temp = ((temp * 3125) / 10 - chip->curr_offset) / 1000;

  if (sign)
    temp *= -1;

	chip->curr = temp;
  RTINFO("[RT5025] current: %d\n", chip->curr);
}

static void rt5025_get_external_temp(struct i2c_client *client)
{
  u8 data[2];
  long int temp;

  if (rt5025_read_reg(client, RT5025_REG_EXT_TEMPERATUE_MSB, data, 2) < 0) {
    printk(KERN_ERR "%s: Failed to read TEMPERATURE\n", __func__);
  }
  chip->ain_volt = (data[0] * 256 + data[1]) * 61 / 100;
  /// Check battery present
	if (chip->ain_volt < 1150)
		chip->online = true;
	else
		chip->online = false;
  temp =  (chip->ain_volt * (-91738) + 81521000) / 100000;
  chip->ext_temp = (int)temp;
	
	if (chip->ext_temp >= HIGH_TEMP_THRES) {
		if (chip->health != POWER_SUPPLY_HEALTH_OVERHEAT)
			chip->temp_high_cnt++;
	} else if (chip->ext_temp <= HIGH_TEMP_RECOVER && chip->ext_temp >= LOW_TEMP_RECOVER) {
		if (chip->health == POWER_SUPPLY_HEALTH_OVERHEAT ||
		    chip->health == POWER_SUPPLY_HEALTH_COLD)
			chip->temp_recover_cnt++;
	} else if (chip->ext_temp <= LOW_TEMP_THRES) {
		if (chip->health != POWER_SUPPLY_HEALTH_COLD)
			chip->temp_low_cnt++;
	} else {
		chip->temp_high_cnt = 0;
		chip->temp_low_cnt = 0;
		chip->temp_recover_cnt = 0;
	}
	
	if (chip->temp_high_cnt >= TEMP_ABNORMAL_COUNT) {
	 chip->health = POWER_SUPPLY_HEALTH_OVERHEAT;
	 chip->temp_high_cnt = 0;
	} else if (chip->temp_low_cnt >= TEMP_ABNORMAL_COUNT) {
	 chip->health = POWER_SUPPLY_HEALTH_COLD;
	 chip->temp_low_cnt = 0;
	} else if (chip->temp_recover_cnt >= TEMP_ABNORMAL_COUNT) {
	 chip->health = POWER_SUPPLY_HEALTH_GOOD;
	 chip->temp_recover_cnt = 0;
	}
  RTINFO("[RT5025] external temperature: %d\n", chip->ext_temp);
}

static void rt5025_clear_cc(operation_mode mode)
{  
  u8 data[2];
	
  if (rt5025_read_reg(chip->client, RT5025_REG_CHANNEL_MSB, data, 2) < 0){
    printk(KERN_ERR "%s: failed to read channel\n", __func__);
  }

  if (mode == CHG)
		data[0] = data[0] | CHANNEL_H_BIT_CLRQCHG;
	else
		data[0] = data[0] | CHANNEL_H_BIT_CLRQDCHG;
		
  if (rt5025_write_reg(chip->client, RT5025_REG_CHANNEL_MSB, data, 2) < 0){
    printk(KERN_ERR "%s: failed to write channel\n", __func__);
  }
}

static void rt5025_get_chg_cc(struct i2c_client *client)
{
  u8 data[4];
  u32 qh_old,ql_old,qh_new,ql_new;
  u32 cc_masec,offset;
  
  if (rt5025_read_reg(client, RT5025_REG_QCHGH_MSB, data, 4) < 0){
    printk(KERN_ERR "%s: Failed to read QCHG\n", __func__);
  }
  qh_old = (data[0]<<8) + data[1];
  ql_old = (data[2]<<8) + data[3];
  
  if (rt5025_read_reg(client, RT5025_REG_QCHGH_MSB, data, 4) < 0){
    printk(KERN_ERR "%s: Failed to read QCHG\n", __func__);
  }
  qh_new = (data[0]<<8) + data[1];
  ql_new = (data[2]<<8) + data[3];
   	
  if (qh_new > qh_old){
     cc_masec = (((qh_new<<16) + ql_new) * 50134) / 10;
  }else if (qh_new == qh_old){
    if (ql_new >= ql_old){
      cc_masec = (((qh_new<<16) + ql_new) * 50134) / 10;
    }else {  
      cc_masec = (((qh_old<<16) + ql_old) * 50134) / 10;
		}
  }	
  
	offset = chip->curr_offset * chip->time_interval;
		   	  
  if (cc_masec != 0){
		cc_masec = (cc_masec - offset) / 1000;
	}

  RTINFO("[RT5025] chg_cc_mAsec: %d\n", cc_masec);

	chip->chg_cc = (cc_masec + chip->chg_cc_unuse) / 3600;
  chip->chg_cc_unuse = (cc_masec + chip->chg_cc_unuse) % 3600;
  RTINFO("[RT5025] chg_cc_mAH: %d\n", chip->chg_cc);
  rt5025_clear_cc(CHG);
}

static void rt5025_get_dchg_cc(struct i2c_client *client)
{
  u8 data[4];
  u32 qh_old,ql_old,qh_new,ql_new;
  u32 cc_masec,offset;
  
  if (rt5025_read_reg(client, RT5025_REG_QDCHGH_MSB, data, 4) < 0){
    printk(KERN_ERR "%s: Failed to read QDCHG\n", __func__);
  }
  qh_old = (data[0]<<8) + data[1];
  ql_old = (data[2]<<8) + data[3];
  
  if (rt5025_read_reg(client, RT5025_REG_QDCHGH_MSB, data, 4) < 0){
    printk(KERN_ERR "%s: Failed to read QDCHG\n", __func__);
  }
  qh_new = (data[0]<<8) + data[1];
  ql_new = (data[2]<<8) + data[3];
  
  if (qh_new > qh_old){
     cc_masec = (((qh_new<<16) + ql_new) * 50134) / 10;
  }else if (qh_new == qh_old){
    if (ql_new >= ql_old){
      cc_masec = (((qh_new<<16) + ql_new) * 50134) / 10;
    }else {  
      cc_masec = (((qh_old<<16) + ql_old) * 50134) / 10;
		}
  }	
  
	offset = chip->curr_offset * chip->time_interval;
		   	  
  if (cc_masec != 0){
		cc_masec = (cc_masec - offset) / 1000;
	}

  RTINFO("[RT5025] dchg_cc_mAsec: %d\n", cc_masec);

	chip->dchg_cc = (cc_masec + chip->dchg_cc_unuse) / 3600;
  chip->dchg_cc_unuse = (cc_masec + chip->dchg_cc_unuse) % 3600;
  RTINFO("[RT5025] dchg_cc_mAH: %d\n", chip->dchg_cc);
	rt5025_clear_cc(DCHG);
}

static void rt5025_get_irq_flag(struct i2c_client *client)
{
  u8 data[1];

  if (rt5025_read_reg(client, RT5025_REG_IRQ_FLAG, data, 1) < 0){
    printk(KERN_ERR "%s: Failed to read irq_flag\n", __func__);
  }
		
  chip->irq_flag = data[0];
  RTINFO("[RT5025] IRQ_FLG 0x%x\n", chip->irq_flag);
}

static void rt5025_get_timer(struct i2c_client *client)
{
  u8 data[2];
	u16 gauge_timer;
	
  if (rt5025_read_reg(client, RT5025_REG_TIMER, data, 2) < 0){
    printk(KERN_ERR "%s: Failed to read Timer\n", __func__);
  }
		
  gauge_timer = (data[0] << 8) + data[1];
  if (gauge_timer > chip->pre_gauge_timer)
		chip->time_interval = gauge_timer - chip->pre_gauge_timer;
	else	
		chip->time_interval = 65536 - chip->pre_gauge_timer + gauge_timer;
		
  chip->pre_gauge_timer = gauge_timer;
  RTINFO("[RT5025] timer %d , interval %d\n", gauge_timer,chip->time_interval);
}

static void rt5025_get_soc(struct i2c_client *client)
{
  chip->soc = 50;
}

static void rt5025_channel_cc(bool enable)
{
  u8 data[1];
	
  if (rt5025_read_reg(chip->client, RT5025_REG_CHANNEL_LSB, data, 1) < 0){
    printk(KERN_ERR "%s: failed to read channel\n", __func__);
  }

  if (enable){
    data[0] = data[0] | 0x80;
  }else { 
    data[0] = data[0] & 0x7F;
  }
    
  if (rt5025_write_reg(chip->client, RT5025_REG_CHANNEL_LSB, data, 1) < 0){
    printk(KERN_ERR "%s: failed to write channel\n", __func__);
  }
}

static void rt5025_register_init(struct i2c_client *client)
{  
  u8 data[1];
	
  /* enable the channel of current,qc,ain,vbat and vadc */
  if (rt5025_read_reg(client, RT5025_REG_CHANNEL_LSB, data, 1) < 0){
    printk("%s: failed to read channel\n", __func__);
  }
  data[0] = data[0] |
						CHANNEL_L_BIT_CADC_EN |
						CHANNEL_L_BIT_AINCH |
						CHANNEL_L_BIT_VBATSCH |
						CHANNEL_L_BIT_VADC_EN;
  if (rt5025_write_reg(client, RT5025_REG_CHANNEL_LSB, data, 1) < 0){
    printk("%s: failed to write channel\n", __func__);
  }
	/* set the alert threshold value */
	irq_thres[MAXTEMP]  = TALRTMAX_VALUE;
	irq_thres[MINTEMP]  = TALRTMIN_VALUE;
	irq_thres[MAXVOLT]  = VALRTMAX_VALUE;
	irq_thres[MINVOLT1] = VALRTMIN1_VALUE;
	irq_thres[MINVOLT2] = VALRTMIN2_VALUE;
	irq_thres[TEMP_RLS] = TRLS_VALUE;
	irq_thres[VOLT_RLS] = VRLS_VALUE;

	chip->chg_cc_unuse = 0;
	chip->dchg_cc_unuse = 0;
	chip->pre_gauge_timer = 0;
	chip->online = 1;
	chip->status = POWER_SUPPLY_STATUS_DISCHARGING;
	chip->health = POWER_SUPPLY_HEALTH_GOOD;
  RTINFO("[RT5025] register initialized\n");
}

static void rt5025_alert_setting(alert_type type, bool enable)
{
	u8 data[1];
	
  if (rt5025_read_reg(chip->client, RT5025_REG_IRQ_CTL, data, 1) < 0){
    printk(KERN_ERR "%s: Failed to read CONFIG\n", __func__);
  }

  if(enable){
		switch(type){
			case MAXTEMP:
				data[0] |= IRQ_CTL_BIT_TMX; //Enable max temperature alert
				chip->max_temp_irq = true;
				RTINFO("Enable min temperature alert");
				break;
			case MINTEMP:
				data[0] |= IRQ_CTL_BIT_TMN; //Enable min temperature alert
				chip->min_temp_irq = true;  
				RTINFO("Enable max temperature alert");
				break;
			case MAXVOLT:
				data[0] |= IRQ_CTL_BIT_VMX; //Enable max voltage alert
				chip->max_volt_irq = true;
				RTINFO("Enable max voltage alert");
				break;
			case MINVOLT1:
				data[0] |= IRQ_CTL_BIT_VMN1; //Enable min1 voltage alert	
				chip->min_volt1_irq = true;
				RTINFO("Enable min1 voltage alert");
				break;
			case MINVOLT2:
				data[0] |= IRQ_CTL_BIT_VMN2; //Enable min2 voltage alert
				chip->min_volt2_irq = true;
				RTINFO("Enable min2 voltage alert");
				break;
			default:
				break;
		}
	}else{
		switch(type){
			case MAXTEMP:
				data[0] = data[0] &~ IRQ_CTL_BIT_TMX; //Disable max temperature alert
				chip->max_temp_irq = false;
				RTINFO("Disable min temperature alert");
				break;
			case MINTEMP:
				data[0] = data[0] &~ IRQ_CTL_BIT_TMN; //Disable min temperature alert
				chip->min_temp_irq = false;
				RTINFO("Disable max temperature alert");
				break;
			case MAXVOLT:
				data[0] = data[0] &~ IRQ_CTL_BIT_VMX; //Disable max voltage alert
				chip->max_volt_irq = false;
				RTINFO("Disable max voltage alert");
				break;
			case MINVOLT1:
				data[0] = data[0] &~ IRQ_CTL_BIT_VMN1; //Disable min1 voltage alert	
				chip->min_volt1_irq = false;
				RTINFO("Disable min1 voltage alert");
				break;
			case MINVOLT2:
				data[0] = data[0] &~ IRQ_CTL_BIT_VMN2; //Disable min2 voltage alert
				chip->min_volt2_irq = false;
				RTINFO("Disable min2 voltage alert");
				break;
			default:
				break;
		}
	}
  if (rt5025_write_reg(chip->client, RT5025_REG_IRQ_CTL, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write IRQ control\n", __func__);
}	
static void rt5025_alert_threshold_init(struct i2c_client *client)
{
  u8 data[1];

  /* TALRT MAX threshold setting */
  data[0] = irq_thres[MAXTEMP];
  if (rt5025_write_reg(client, RT5025_REG_TALRT_MAXTH, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write TALRT MAX threshold\n", __func__);	
  /* TALRT MIN threshold setting */
  data[0] = irq_thres[MINTEMP];
  if (rt5025_write_reg(client, RT5025_REG_TALRT_MINTH, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write TALRT MIN threshold\n", __func__);	
  /* VALRT MAX threshold setting */
  data[0] = irq_thres[MAXVOLT];
  if (rt5025_write_reg(client, RT5025_REG_VALRT_MAXTH, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write VALRT MAX threshold\n", __func__);	
  /* VALRT MIN1 threshold setting */
  data[0] = irq_thres[MINVOLT1];
  if (rt5025_write_reg(client, RT5025_REG_VALRT_MIN1TH, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write VALRT MIN1 threshold\n", __func__);	
  /* VALRT MIN2 threshold setting */
  data[0] = irq_thres[MINVOLT2];
  if (rt5025_write_reg(client, RT5025_REG_VALRT_MIN2TH, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write VALRT MIN2 threshold\n", __func__);	
}

static void rt5025_alert_init(struct i2c_client *client)
{
  /* Set RT5025 gauge alert configuration */
  rt5025_alert_threshold_init(client);
	/* Enable gauge alert function */
	rt5025_alert_setting(MAXTEMP,true);
	rt5025_alert_setting(MINTEMP,true);
	rt5025_alert_setting(MAXVOLT,true);
	rt5025_alert_setting(MINVOLT1,true);
	rt5025_alert_setting(MINVOLT2,true);	
}

void rt5025_irq_handler(void)
{
  rt5025_get_irq_flag(chip->client);

  if ((chip->irq_flag) & IRQ_FLG_BIT_TMX){	
		printk(KERN_INFO "[RT5025]: Min temperature IRQ received\n");
		rt5025_alert_setting(MAXTEMP,false);
		chip->max_temp_irq = false;
	}
  if ((chip->irq_flag) & IRQ_FLG_BIT_TMN){
		printk(KERN_INFO "[RT5025]: Max temperature IRQ received\n");
		rt5025_alert_setting(MINTEMP,false);
		chip->min_temp_irq = false; 
	}
  if ((chip->irq_flag) & IRQ_FLG_BIT_VMX){
		printk(KERN_INFO "[RT5025]: Max voltage IRQ received\n");
		rt5025_alert_setting(MAXVOLT,false);
		chip->max_volt_irq = false;
	}
  if ((chip->irq_flag) & IRQ_FLG_BIT_VMN1){
		printk(KERN_INFO "[RT5025]: Min voltage1 IRQ received\n");
		rt5025_alert_setting(MINVOLT1,false);
		chip->min_volt1_irq = false;
	}
  if ((chip->irq_flag) & IRQ_FLG_BIT_VMN2){
		printk(KERN_INFO "[RT5025]: Min voltage2 IRQ received\n");
		rt5025_alert_setting(MINVOLT2,false);
		chip->min_volt2_irq = false;
	}
	
	wake_lock(&chip->monitor_wake_lock);
	schedule_delayed_work(&chip->monitor_work, 0);
}

static void rt5025_update(struct i2c_client *client)
{
  /* Update voltage */
  rt5025_get_vcell(client);
  /* Update current */
  rt5025_get_current(client);
  /* Update external temperature */
  rt5025_get_external_temp(client);
  /* Read timer */
  rt5025_get_timer(client);
  /* Update chg cc */
  rt5025_get_chg_cc(client);
  /* Update dchg cc */
  rt5025_get_dchg_cc(client);
  /* Update SOC */
  rt5025_get_soc(client);

  if ((chip->max_temp_irq == false) &&
		 (((irq_thres[MAXTEMP] * IRQ_THRES_UNIT) / 100 - chip->ain_volt) > irq_thres[TEMP_RLS])){
		rt5025_alert_setting(MAXTEMP,true);
	}else if ((chip->min_temp_irq == false) &&
					  ((chip->ain_volt - (irq_thres[MINTEMP] * IRQ_THRES_UNIT) / 100) > irq_thres[TEMP_RLS])){
		rt5025_alert_setting(MINTEMP,true);
	}else if ((chip->max_volt_irq == false) &&
					((((irq_thres[MAXVOLT] * IRQ_THRES_UNIT) / 100) - chip->vcell) > irq_thres[VOLT_RLS])){
		rt5025_alert_setting(MAXVOLT,true);
	}else if ((chip->min_volt1_irq == false) &&
					((chip->vcell - ((irq_thres[MINVOLT1] * IRQ_THRES_UNIT) / 100)) > irq_thres[VOLT_RLS])){
		rt5025_alert_setting(MINVOLT1,true);				
	}else if ((chip->min_volt2_irq == false) &&
					((chip->vcell - ((irq_thres[MINVOLT2] * IRQ_THRES_UNIT) / 100)) > irq_thres[VOLT_RLS])){
		rt5025_alert_setting(MINVOLT2,true);						
	}
}

static void rt5025_update_work(struct work_struct *work)
{
	unsigned long flags;
	
  rt5025_update(chip->client);

  /* Update data to framework */
  power_supply_changed(&chip->battery);
  
	/* prevent suspend before starting the alarm */
	local_irq_save(flags);
	chip->last_poll = alarm_get_elapsed_realtime();
	rt5025_program_alarm(NORMAL_POLL);
	local_irq_restore(flags);

	wake_unlock(&chip->monitor_wake_lock);
}

static enum power_supply_property rt5025_battery_props[] = {
  POWER_SUPPLY_PROP_STATUS,
  POWER_SUPPLY_PROP_HEALTH,
  POWER_SUPPLY_PROP_PRESENT,
  POWER_SUPPLY_PROP_TEMP,
  POWER_SUPPLY_PROP_ONLINE,
  POWER_SUPPLY_PROP_VOLTAGE_NOW,
  POWER_SUPPLY_PROP_CURRENT_NOW,
  POWER_SUPPLY_PROP_CAPACITY,
  POWER_SUPPLY_PROP_TECHNOLOGY,
};

void rt5025_gauge_suspend(void)
{
	rt5025_channel_cc(false);
	cancel_delayed_work(&chip->monitor_work);

  RTINFO("\n");
}

void rt5025_gauge_resume(void)
{
	rt5025_channel_cc(true);
	wake_lock(&chip->monitor_wake_lock);
	schedule_delayed_work(&chip->monitor_work, 0);
  RTINFO("\n");
}

void rt5025_gauge_remove(void)
{
	chip->info->event_callback = NULL;
	power_supply_unregister(&chip->battery);
	cancel_delayed_work(&chip->monitor_work);
	wake_lock_destroy(&chip->monitor_wake_lock);
	kfree(chip);
}

int rt5025_gauge_init(struct rt5025_power_info *info)
{
	int ret;
  chip = kzalloc(sizeof(*chip), GFP_KERNEL);
  if (!chip)
    return -ENOMEM;

  chip->client = info->i2c;
  chip->info = info;  
  chip->battery.name = "rt5025-battery";
  chip->battery.type = POWER_SUPPLY_TYPE_BATTERY;
  chip->battery.get_property = rt5025_get_property;
  chip->battery.properties = rt5025_battery_props;
  chip->battery.num_properties = ARRAY_SIZE(rt5025_battery_props);

  ret = power_supply_register(info->dev, &chip->battery);
  if (ret) {
    printk(KERN_ERR "[RT5025] power supply register failed\n");
		goto err_wake_lock;
  }

  chip->last_poll = alarm_get_elapsed_realtime();
	alarm_init(&chip->wakeup_alarm, ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP,
		rt5025_gauge_alarm);

	INIT_DELAYED_WORK(&chip->monitor_work, rt5025_update_work);
	
	wake_lock_init(&chip->monitor_wake_lock, WAKE_LOCK_SUSPEND,
			"rt-battery-monitor");
  /* enable channel */
  rt5025_register_init(info->i2c);

	/* enable gauge IRQ */
  rt5025_alert_init(info->i2c);

	/* register callback functions */
	chip->cb.rt5025_gauge_irq_handler = rt5025_irq_handler;
	chip->cb.rt5025_gauge_set_status = rt5025_set_status;
	chip->cb.rt5025_gauge_suspend = rt5025_gauge_suspend;
	chip->cb.rt5025_gauge_resume = rt5025_gauge_resume;
	chip->cb.rt5025_gauge_remove = rt5025_gauge_remove;
	info->event_callback=&chip->cb;

	//rt_register_gauge_callbacks(info->i2c, &chip->cb);
	
	wake_lock(&chip->monitor_wake_lock);
	schedule_delayed_work(&chip->monitor_work, msecs_to_jiffies(1000));

  return 0;

err_wake_lock:
	wake_lock_destroy(&chip->monitor_wake_lock);
	kfree(chip);

	return ret;
}
