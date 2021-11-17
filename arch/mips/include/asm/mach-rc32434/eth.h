/*
 *  Definitions for the Ethernet registers
 *
 *  Copyright 2002 Allend Stichter <allen.stichter@idt.com>
 *  Copyright 2008 Florian Fainelli <florian@openwrt.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __ASM_RC32434_ETH_H
#define __ASM_RC32434_ETH_H


#define ETH0_BASE_ADDR		0x18060000

struct eth_regs {
	u32 ethintfc;
	u32 ethfifott;
	u32 etharc;
	u32 ethhash0;
	u32 ethhash1;
	u32 ethu0[4];		/* Reserved. */
	u32 ethpfs;
	u32 ethmcp;
	u32 eth_u1[10];		/* Reserved. */
	u32 ethspare;
	u32 eth_u2[42];		/* Reserved. */
	u32 ethsal0;
	u32 ethsah0;
	u32 ethsal1;
	u32 ethsah1;
	u32 ethsal2;
	u32 ethsah2;
	u32 ethsal3;
	u32 ethsah3;
	u32 ethrbc;
	u32 ethrpc;
	u32 ethrupc;
	u32 ethrfc;
	u32 ethtbc;
	u32 ethgpf;
	u32 eth_u9[50];		/* Reserved. */
	u32 ethmac1;
	u32 ethmac2;
	u32 ethipgt;
	u32 ethipgr;
	u32 ethclrt;
	u32 ethmaxf;
	u32 eth_u10;		/* Reserved. */
	u32 ethmtest;
	u32 miimcfg;
	u32 miimcmd;
	u32 miimaddr;
	u32 miimwtd;
	u32 miimrdd;
	u32 miimind;
	u32 eth_u11;		/* Reserved. */
	u32 eth_u12;		/* Reserved. */
	u32 ethcfsa0;
	u32 ethcfsa1;
	u32 ethcfsa2;
};

/* Ethernet interrupt registers */
#define ETH_INT_FC_EN		(1 << 0)
#define ETH_INT_FC_ITS		(1 << 1)
#define ETH_INT_FC_RIP		(1 << 2)
#define ETH_INT_FC_JAM		(1 << 3)
#define ETH_INT_FC_OVR		(1 << 4)
#define ETH_INT_FC_UND		(1 << 5)
#define ETH_INT_FC_IOC		0x000000c0

/* Ethernet FIFO registers */
#define ETH_FIFI_TT_TTH_BIT	0
#define ETH_FIFO_TT_TTH		0x0000007f

/* Ethernet ARC/multicast registers */
#define ETH_ARC_PRO		(1 << 0)
#define ETH_ARC_AM		(1 << 1)
#define ETH_ARC_AFM		(1 << 2)
#define ETH_ARC_AB		(1 << 3)

/* Ethernet SAL registers */
#define ETH_SAL_BYTE_5		0x000000ff
#define ETH_SAL_BYTE_4		0x0000ff00
#define ETH_SAL_BYTE_3		0x00ff0000
#define ETH_SAL_BYTE_2		0xff000000

/* Ethernet SAH registers */
#define ETH_SAH_BYTE1		0x000000ff
#define ETH_SAH_BYTE0		0x0000ff00

/* Ethernet GPF register */
#define ETH_GPF_PTV		0x0000ffff

/* Ethernet PFG register */
#define ETH_PFS_PFD		(1 << 0)

/* Ethernet CFSA[0-3] registers */
#define ETH_CFSA0_CFSA4		0x000000ff
#define ETH_CFSA0_CFSA5		0x0000ff00
#define ETH_CFSA1_CFSA2		0x000000ff
#define ETH_CFSA1_CFSA3		0x0000ff00
#define ETH_CFSA1_CFSA0		0x000000ff
#define ETH_CFSA1_CFSA1		0x0000ff00

/* Ethernet MAC1 registers */
#define ETH_MAC1_RE		(1 << 0)
#define ETH_MAC1_PAF		(1 << 1)
#define ETH_MAC1_RFC		(1 << 2)
#define ETH_MAC1_TFC		(1 << 3)
#define ETH_MAC1_LB		(1 << 4)
#define ETH_MAC1_MR		(1 << 31)

/* Ethernet MAC2 registers */
#define ETH_MAC2_FD		(1 << 0)
#define ETH_MAC2_FLC		(1 << 1)
#define ETH_MAC2_HFE		(1 << 2)
#define ETH_MAC2_DC		(1 << 3)
#define ETH_MAC2_CEN		(1 << 4)
#define ETH_MAC2_PE		(1 << 5)
#define ETH_MAC2_VPE		(1 << 6)
#define ETH_MAC2_APE		(1 << 7)
#define ETH_MAC2_PPE		(1 << 8)
#define ETH_MAC2_LPE		(1 << 9)
#define ETH_MAC2_NB		(1 << 12)
#define ETH_MAC2_BP		(1 << 13)
#define ETH_MAC2_ED		(1 << 14)

/* Ethernet IPGT register */
#define ETH_IPGT		0x0000007f

/* Ethernet IPGR registers */
#define ETH_IPGR_IPGR2		0x0000007f
#define ETH_IPGR_IPGR1		0x00007f00

/* Ethernet CLRT registers */
#define ETH_CLRT_MAX_RET	0x0000000f
#define ETH_CLRT_COL_WIN	0x00003f00

/* Ethernet MAXF register */
#define ETH_MAXF		0x0000ffff

/* Ethernet test registers */
#define ETH_TEST_REG		(1 << 2)
#define ETH_MCP_DIV		0x000000ff

/* MII registers */
#define ETH_MII_CFG_RSVD	0x0000000c
#define ETH_MII_CMD_RD		(1 << 0)
#define ETH_MII_CMD_SCN		(1 << 1)
#define ETH_MII_REG_ADDR	0x0000001f
#define ETH_MII_PHY_ADDR	0x00001f00
#define ETH_MII_WTD_DATA	0x0000ffff
#define ETH_MII_RDD_DATA	0x0000ffff
#define ETH_MII_IND_BSY		(1 << 0)
#define ETH_MII_IND_SCN		(1 << 1)
#define ETH_MII_IND_NV		(1 << 2)

/*
 * Values for the DEVCS field of the Ethernet DMA Rx and Tx descriptors.
 */

#define ETH_RX_FD		(1 << 0)
#define ETH_RX_LD		(1 << 1)
#define ETH_RX_ROK		(1 << 2)
#define ETH_RX_FM		(1 << 3)
#define ETH_RX_MP		(1 << 4)
#define ETH_RX_BP		(1 << 5)
#define ETH_RX_VLT		(1 << 6)
#define ETH_RX_CF		(1 << 7)
#define ETH_RX_OVR		(1 << 8)
#define ETH_RX_CRC		(1 << 9)
#define ETH_RX_CV		(1 << 10)
#define ETH_RX_DB		(1 << 11)
#define ETH_RX_LE		(1 << 12)
#define ETH_RX_LOR		(1 << 13)
#define ETH_RX_CES		(1 << 14)
#define ETH_RX_LEN_BIT		16
#define ETH_RX_LEN		0xffff0000

#define ETH_TX_FD		(1 << 0)
#define ETH_TX_LD		(1 << 1)
#define ETH_TX_OEN		(1 << 2)
#define ETH_TX_PEN		(1 << 3)
#define ETH_TX_CEN		(1 << 4)
#define ETH_TX_HEN		(1 << 5)
#define ETH_TX_TOK		(1 << 6)
#define ETH_TX_MP		(1 << 7)
#define ETH_TX_BP		(1 << 8)
#define ETH_TX_UND		(1 << 9)
#define ETH_TX_OF		(1 << 10)
#define ETH_TX_ED		(1 << 11)
#define ETH_TX_EC		(1 << 12)
#define ETH_TX_LC		(1 << 13)
#define ETH_TX_TD		(1 << 14)
#define ETH_TX_CRC		(1 << 15)
#define ETH_TX_LE		(1 << 16)
#define ETH_TX_CC		0x001E0000

#endif	/* __ASM_RC32434_ETH_H */
