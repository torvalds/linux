/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************
*/


#ifndef __RTMP_PCI_H__
#define __RTMP_PCI_H__

#define RT28XX_HANDLE_DEV_ASSIGN(handle, dev_p)				\
	((POS_COOKIE)handle)->pci_dev = dev_p;


#ifdef LINUX
// set driver data
#define RT28XX_DRVDATA_SET(_a)			pci_set_drvdata(_a, net_dev);

#define RT28XX_PUT_DEVICE(dev_p)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#define SA_SHIRQ IRQF_SHARED
#endif

#ifdef PCI_MSI_SUPPORT
#define RTMP_MSI_ENABLE(_pAd) \
{	POS_COOKIE _pObj = (POS_COOKIE)(_pAd->OS_Cookie); \
	(_pAd)->HaveMsi =	pci_enable_msi(_pObj->pci_dev) == 0 ? TRUE : FALSE; }

#define RTMP_MSI_DISABLE(_pAd) \
{	POS_COOKIE _pObj = (POS_COOKIE)(_pAd->OS_Cookie); \
	if (_pAd->HaveMsi == TRUE) \
		pci_disable_msi(_pObj->pci_dev); \
	_pAd->HaveMsi = FALSE;	}
#else
#define RTMP_MSI_ENABLE(_pAd)
#define RTMP_MSI_DISABLE(_pAd)
#endif // PCI_MSI_SUPPORT //


#define RTMP_PCI_DEV_UNMAP()										\
{	if (net_dev->base_addr)	{								\
		iounmap((void *)(net_dev->base_addr));				\
		release_mem_region(pci_resource_start(dev_p, 0),	\
							pci_resource_len(dev_p, 0)); }	\
	if (net_dev->irq) pci_release_regions(dev_p); }


#define RTMP_IRQ_REQUEST(net_dev)							\
{	PRTMP_ADAPTER _pAd = (PRTMP_ADAPTER)(RTMP_OS_NETDEV_GET_PRIV(net_dev));	\
	POS_COOKIE _pObj = (POS_COOKIE)(_pAd->OS_Cookie);		\
	RTMP_MSI_ENABLE(_pAd);									\
	if ((retval = request_irq(_pObj->pci_dev->irq,		\
							rt2860_interrupt, SA_SHIRQ,		\
							(net_dev)->name, (net_dev)))) {	\
		DBGPRINT(RT_DEBUG_ERROR, ("request_irq  error(%d)\n", retval));	\
	return retval; } }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#define RTMP_IRQ_RELEASE(net_dev)								\
{	PRTMP_ADAPTER _pAd = (PRTMP_ADAPTER)(RTMP_OS_NETDEV_GET_PRIV(net_dev));		\
	POS_COOKIE _pObj = (POS_COOKIE)(_pAd->OS_Cookie);			\
	synchronize_irq(_pObj->pci_dev->irq);						\
	free_irq(_pObj->pci_dev->irq, (net_dev));					\
	RTMP_MSI_DISABLE(_pAd); }
#else
#define RTMP_IRQ_RELEASE(net_dev)								\
{	PRTMP_ADAPTER _pAd = (PRTMP_ADAPTER)(RTMP_OS_NETDEV_GET_PRIV(net_dev));		\
	POS_COOKIE _pObj = (POS_COOKIE)(_pAd->OS_Cookie);			\
	free_irq(_pObj->pci_dev->irq, (net_dev));					\
	RTMP_MSI_DISABLE(_pAd); }
#endif

#define PCI_REG_READ_WORD(pci_dev, offset, Configuration)   \
    if (pci_read_config_word(pci_dev, offset, &reg16) == 0)     \
        Configuration = le2cpu16(reg16);                        \
    else                                                        \
        Configuration = 0;

#define PCI_REG_WIRTE_WORD(pci_dev, offset, Configuration)  \
    reg16 = cpu2le16(Configuration);                        \
    pci_write_config_word(pci_dev, offset, reg16);          \

#endif // LINUX //




#endif // __RTMP_PCI_H__ //
