/*******************************************************************************
  STMMAC external timer Header File.

  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

struct stmmac_timer {
	void (*timer_start) (unsigned int new_freq);
	void (*timer_stop) (void);
	unsigned int freq;
	unsigned int enable;
};

/* Open the HW timer device and return 0 in case of success */
int stmmac_open_ext_timer(struct net_device *dev, struct stmmac_timer *tm);
/* Stop the timer and release it */
int stmmac_close_ext_timer(void);
/* Function used for scheduling task within the stmmac */
void stmmac_schedule(struct net_device *dev);

#if defined(CONFIG_STMMAC_TMU_TIMER)
extern int tmu2_register_user(void *fnt, void *data);
extern void tmu2_unregister_user(void);
#endif
