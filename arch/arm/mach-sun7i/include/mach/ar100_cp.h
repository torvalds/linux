/*
 *  arch/arm/mach-xxx/include/ar100_cp.h
 *
 * Copyright 2012 (c) Allwinner
 * kevin (kevin@allwinnertech.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

typedef int (*ar100_cb_t)(void *arg);



/*
 * get value from axp register.
 * addr:  register address;
 * value: value get from axp;
 * return: result, 0 - get register successed, !0 - get register failed;
 */
int ar100_axp_readreg(unsigned long addr, unsigned long *value);


/*
 *set value for axp register.
 *addr:  register address;
 *value: value to be set;
 *return: result, 0 - set register successed, !0 - set register failed;
 */
int ar100_axp_writereg(unsigned long addr, unsigned long *value);


/*
 * axp get battery paramter.
 * para:  battery parameter;
 * return: result, 0 - get battery successed, !0 - get battery failed;
 */
int ar100_axp_getbat(void *para);


/*
 * axp set battery paramter.
 * para:  battery parameter;
 * return: result, 0 - set battery successed, !0 - set battery failed;
 */
int ar100_axp_setbat(void *para);


/*
 * set target frequency.
 * freq:  target frequency to be set, based on KHZ.
 * return: result, 0 - set frequency successed, !0 - set frequency failed;
 */
int ar100_dvfs_setcpufreq(unsigned long freq);


/*
 * enter normal standby.
 * para:  parameter for enter normal standby.
 * return: result, 0 - normal standby successed, !0 - normal standby failed;
 */
int ar100_standby_normal(void *para);


/*
 * enter super standby.
 * para:  parameter for enter normal standby.
 * return: result, 0 - super standby successed, !0 - super standby failed;
 */
int ar100_standby_super(void *para);


/*
 * register call-back function, call-back function is for ar100 notify some event to ac327,
 * axp interrupt for ex.
 * func:  call-back function;
 * para:  parameter for call-back function;
 * return: result, 0 - register call-back function successed;
 *                !0 - register call-back function failed;
 * NOTE: the function is like "int callback(void *para)";
 */
int ar100_cb_register(ar100_cb_t *func, void *para);


/*
 * unregister call-back function.
 * func:  call-back function which need be unregister;
 */
void ar100_cb_unregister(ar100_cb_t *func);

