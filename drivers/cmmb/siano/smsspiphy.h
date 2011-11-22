/****************************************************************

Siano Mobile Silicon, Inc.
MDTV receiver kernel modules.
Copyright (C) 2006-2008, Uri Shkolnik

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

 This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************/

#ifndef __SMS_SPI_PHY_H__
#define __SMS_SPI_PHY_H__

void smsspibus_xfer(void *context, unsigned char *txbuf,
		    unsigned long txbuf_phy_addr, unsigned char *rxbuf,
		    unsigned long rxbuf_phy_addr, int len);
void *smsspiphy_init(void *context, void (*smsspi_interruptHandler) (void *),
		     void *intr_context);
int smsspiphy_deinit(void *context);
void smschipreset(void *context);
void WriteFWtoStellar(void *pSpiPhy, unsigned char *pFW, unsigned long Len);
void prepareForFWDnl(void *pSpiPhy);
void fwDnlComplete(void *context, int App);
void smsspibus_ssp_suspend(void* context );
int  smsspibus_ssp_resume(void* context);

 struct cmmb_io_def_s
{
	unsigned int cmmb_pw_en;
	unsigned int cmmb_pw_dwn;
	unsigned int cmmb_pw_rst;
	unsigned int cmmb_irq;
	void (*io_init_mux)(void);
	void (*cmmb_io_pm)(void);
	void (*cmmb_power_on)(void);
	void (*cmmb_power_down)(void);
};
#endif /* __SMS_SPI_PHY_H__ */
