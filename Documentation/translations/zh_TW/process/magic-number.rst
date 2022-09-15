.. SPDX-License-Identifier: GPL-2.0

.. _tw_magicnumbers:

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/process/magic-number.rst <magicnumbers>`

如果想評論或更新本文的內容，請直接發信到LKML。如果你使用英文交流有困難的話，也可
以向中文版維護者求助。如果本翻譯更新不及時或者翻譯存在問題，請聯繫中文版維護者::

        中文版維護者： 賈威威 Jia Wei Wei <harryxiyou@gmail.com>
        中文版翻譯者： 賈威威 Jia Wei Wei <harryxiyou@gmail.com>
        中文版校譯者： 賈威威 Jia Wei Wei <harryxiyou@gmail.com>
                      胡皓文 Hu Haowen <src.res@email.cn>

Linux 魔術數
============

這個文件是有關當前使用的魔術值註冊表。當你給一個結構添加了一個魔術值，你也應該把這個魔術值添加到這個文件，因爲我們最好把用於各種結構的魔術值統一起來。

使用魔術值來保護內核數據結構是一個非常好的主意。這就允許你在運行期檢查(a)一個結構是否已經被攻擊，或者(b)你已經給一個例行程序通過了一個錯誤的結構。後一種情況特別地有用---特別是當你通過一個空指針指向結構體的時候。tty源碼，例如，經常通過特定驅動使用這種方法並且反覆地排列特定方面的結構。

使用魔術值的方法是在結構的開始處聲明的，如下::

        struct tty_ldisc {
	        int	magic;
        	...
        };

當你以後給內核添加增強功能的時候，請遵守這條規則！這樣就會節省數不清的調試時間，特別是一些古怪的情況，例如，數組超出範圍並且重新寫了超出部分。遵守這個規則，‪這些情況可以被快速地，安全地避免。

		Theodore Ts'o
		  31 Mar 94

給當前的Linux 2.1.55添加魔術表。

		Michael Chastain
		<mailto:mec@shout.net>
		22 Sep 1997

現在應該最新的Linux 2.1.112.因爲在特性凍結期間，不能在2.2.x前改變任何東西。這些條目被數域所排序。

		Krzysztof G.Baranowski
	        <mailto: kgb@knm.org.pl>
		29 Jul 1998

更新魔術表到Linux 2.5.45。剛好越過特性凍結，但是有可能還會有一些新的魔術值在2.6.x之前融入到內核中。

		Petr Baudis
		<pasky@ucw.cz>
		03 Nov 2002

更新魔術表到Linux 2.5.74。

		Fabian Frederick
                <ffrederick@users.sourceforge.net>
		09 Jul 2003

===================== ================ ======================== ==========================================
魔術數名              數字             結構                     文件
===================== ================ ======================== ==========================================
PG_MAGIC              'P'              pg_{read,write}_hdr      ``include/linux/pg.h``
HDLC_MAGIC            0x239e           n_hdlc                   ``drivers/char/n_hdlc.c``
APM_BIOS_MAGIC        0x4101           apm_user                 ``arch/x86/kernel/apm_32.c``
FASYNC_MAGIC          0x4601           fasync_struct            ``include/linux/fs.h``
SLIP_MAGIC            0x5302           slip                     ``drivers/net/slip.h``
TTY_MAGIC             0x5401           tty_struct               ``include/linux/tty.h``
MGSL_MAGIC            0x5401           mgsl_info                ``drivers/char/synclink.c``
TTY_DRIVER_MAGIC      0x5402           tty_driver               ``include/linux/tty_driver.h``
MGSLPC_MAGIC          0x5402           mgslpc_info              ``drivers/char/pcmcia/synclink_cs.c``
FULL_DUPLEX_MAGIC     0x6969                                    ``drivers/net/ethernet/dec/tulip/de2104x.c``
CG_MAGIC              0x00090255       ufs_cylinder_group       ``include/linux/ufs_fs.h``
GDTIOCTL_MAGIC        0x06030f07       gdth_iowr_str            ``drivers/scsi/gdth_ioctl.h``
RIEBL_MAGIC           0x09051990                                ``drivers/net/atarilance.c``
NBD_REQUEST_MAGIC     0x12560953       nbd_request              ``include/linux/nbd.h``
BAYCOM_MAGIC          0x19730510       baycom_state             ``drivers/net/baycom_epp.c``
SAVEKMSG_MAGIC2       0x4B4D5347       savekmsg                 ``arch/*/amiga/config.c``
COW_MAGIC             0x4f4f4f4d       cow_header_v1            ``arch/um/drivers/ubd_user.c``
SAVEKMSG_MAGIC1       0x53415645       savekmsg                 ``arch/*/amiga/config.c``
EEPROM_MAGIC_VALUE    0x5ab478d2       lanai_dev                ``drivers/atm/lanai.c``
HDLCDRV_MAGIC         0x5ac6e778       hdlcdrv_state            ``include/linux/hdlcdrv.h``
KV_MAGIC              0x5f4b565f       kernel_vars_s            ``arch/mips/include/asm/sn/klkernvars.h``
FW_HEADER_MAGIC       0x65726F66       fw_header                ``drivers/atm/fore200e.h``
LO_MAGIC              0x68797548       nbd_device               ``include/linux/nbd.h``
NBD_REPLY_MAGIC       0x96744668       nbd_reply                ``include/linux/nbd.h``
ENI155_MAGIC          0xa54b872d       midway_eprom	        ``drivers/atm/eni.h``
CODA_MAGIC            0xC0DAC0DA       coda_file_info           ``fs/coda/coda_fs_i.h``
DPMEM_MAGIC           0xc0ffee11       gdt_pci_sram             ``drivers/scsi/gdth.h``
YAM_MAGIC             0xF10A7654       yam_port                 ``drivers/net/hamradio/yam.c``
CCB_MAGIC             0xf2691ad2       ccb                      ``drivers/scsi/ncr53c8xx.c``
QUEUE_MAGIC_FREE      0xf7e1c9a3       queue_entry              ``drivers/scsi/arm/queue.c``
QUEUE_MAGIC_USED      0xf7e1cc33       queue_entry              ``drivers/scsi/arm/queue.c``
NMI_MAGIC             0x48414d4d455201 nmi_s                    ``arch/mips/include/asm/sn/nmi.h``
===================== ================ ======================== ==========================================


請注意，在聲音記憶管理中仍然有一些特殊的爲每個驅動定義的魔術值。查看include/sound/sndmagic.h來獲取他們完整的列表信息。很多OSS聲音驅動擁有自己從音效卡PCI ID構建的魔術值-他們也沒有被列在這裡。

IrDA子系統也使用了大量的自己的魔術值，查看include/net/irda/irda.h來獲取他們完整的信息。

HFS是另外一個比較大的使用魔術值的文件系統-你可以在fs/hfs/hfs.h中找到他們。

