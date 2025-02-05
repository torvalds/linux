.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/staging/magic-number.rst

:翻译:

 贾威威 Jia Wei Wei <harryxiyou@gmail.com>

:校译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

Linux 魔术数
============

这个文件是有关当前使用的魔术值注册表。当你给一个结构体添加了一个魔术值，你也
应该把这个魔术值添加到这个文件，因为我们最好把用于各种结构体的魔术值统一起来。

使用魔术值来保护内核数据结构是一个 **非常好的主意** 。这就允许你在运行时检
查一个结构体(a)是否已经被攻击，或者(b)你已经给一个例程传递了一个错误的结构
体。最后一种情况特别地有用---特别是当你通过一个空指针指向结构体的时候。例如，
tty源码经常通过特定驱动使用这种方法用来反复地排列特定方面的结构体。

使用魔术值的方法是在结构体的开头声明它们，如下::

        struct tty_ldisc {
	        int	magic;
        	...
        };

当你以后给内核添加增强功能的时候，请遵守这条规则！这样就会节省数不清的调试
时间，特别是一些古怪的情况，例如，数组超出范围并且覆盖写了超出部分。利用这
个规则，这些情况可以被快速地，安全地检测到这些案例。

变更日志::

					Theodore Ts'o
					31 Mar 94

  给当前的Linux 2.1.55添加魔术表。

					Michael Chastain
					<mailto:mec@shout.net>
					22 Sep 1997

  现在应该最新的Linux 2.1.112.因为在特性冻结期间，不能在2.2.x前改变任
  何东西。这些条目被数域所排序。

					Krzysztof G.Baranowski
					<mailto: kgb@knm.org.pl>
					29 Jul 1998

  更新魔术表到Linux 2.5.45。刚好越过特性冻结，但是有可能还会有一些新的魔
  术值在2.6.x之前融入到内核中。

					Petr Baudis
					<pasky@ucw.cz>
					03 Nov 2002

  更新魔术表到Linux 2.5.74。

					Fabian Frederick
					<ffrederick@users.sourceforge.net>
					09 Jul 2003

===================== ================ ======================== ==========================================
魔术数名              数字             结构                     文件
===================== ================ ======================== ==========================================
PG_MAGIC              'P'              pg_{read,write}_hdr      ``include/linux/pg.h``
APM_BIOS_MAGIC        0x4101           apm_user                 ``arch/x86/kernel/apm_32.c``
FASYNC_MAGIC          0x4601           fasync_struct            ``include/linux/fs.h``
SLIP_MAGIC            0x5302           slip                     ``drivers/net/slip.h``
BAYCOM_MAGIC          0x19730510       baycom_state             ``drivers/net/baycom_epp.c``
HDLCDRV_MAGIC         0x5ac6e778       hdlcdrv_state            ``include/linux/hdlcdrv.h``
KV_MAGIC              0x5f4b565f       kernel_vars_s            ``arch/mips/include/asm/sn/klkernvars.h``
CODA_MAGIC            0xC0DAC0DA       coda_file_info           ``fs/coda/coda_fs_i.h``
YAM_MAGIC             0xF10A7654       yam_port                 ``drivers/net/hamradio/yam.c``
CCB_MAGIC             0xf2691ad2       ccb                      ``drivers/scsi/ncr53c8xx.c``
QUEUE_MAGIC_FREE      0xf7e1c9a3       queue_entry              ``drivers/scsi/arm/queue.c``
QUEUE_MAGIC_USED      0xf7e1cc33       queue_entry              ``drivers/scsi/arm/queue.c``
NMI_MAGIC             0x48414d4d455201 nmi_s                    ``arch/mips/include/asm/sn/nmi.h``
===================== ================ ======================== ==========================================
