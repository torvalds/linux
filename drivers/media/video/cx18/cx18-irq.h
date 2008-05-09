/*
 *  cx18 interrupt handling
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#define HW2_I2C1_INT			(1 << 22)
#define HW2_I2C2_INT			(1 << 23)
#define HW2_INT_CLR_STATUS		0xc730c4
#define HW2_INT_MASK5_PCI		0xc730e4
#define SW1_INT_SET                     0xc73100
#define SW1_INT_STATUS                  0xc73104
#define SW1_INT_ENABLE_PCI              0xc7311c
#define SW2_INT_SET                     0xc73140
#define SW2_INT_STATUS                  0xc73144
#define SW2_INT_ENABLE_PCI              0xc7315c

irqreturn_t cx18_irq_handler(int irq, void *dev_id);

void cx18_irq_work_handler(struct work_struct *work);
void cx18_dma_stream_dec_prepare(struct cx18_stream *s, u32 offset, int lock);
void cx18_unfinished_dma(unsigned long arg);
