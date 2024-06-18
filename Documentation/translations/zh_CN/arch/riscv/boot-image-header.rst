.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/arch/riscv/boot-image-header.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_boot-image-header.rst:

==========================
RISC-V Linux启动镜像文件头
==========================

:Author: Atish Patra <atish.patra@wdc.com>
:Date:   20 May 2019

此文档仅描述RISC-V Linux 启动文件头的详情。

TODO:
  写一个完整的启动指南。

在解压后的Linux内核镜像中存在以下64字节的文件头::

	u32 code0;		  /* Executable code */
	u32 code1;		  /* Executable code */
	u64 text_offset;	  /* Image load offset, little endian */
	u64 image_size;		  /* Effective Image size, little endian */
	u64 flags;		  /* kernel flags, little endian */
	u32 version;		  /* Version of this header */
	u32 res1 = 0;		  /* Reserved */
	u64 res2 = 0;		  /* Reserved */
	u64 magic = 0x5643534952; /* Magic number, little endian, "RISCV" */
	u32 magic2 = 0x05435352;  /* Magic number 2, little endian, "RSC\x05" */
	u32 res3;		  /* Reserved for PE COFF offset */

这种头格式与PE/COFF文件头兼容，并在很大程度上受到ARM64文件头的启发。因此，ARM64
和RISC-V文件头可以在未来合并为一个共同的头。

注意
====

- 将来也可以复用这个文件头，用来对RISC-V的EFI桩提供支持。为了使内核镜像如同一个
  EFI应用程序一样加载，EFI规范中规定在内核镜像的开始需要PE/COFF镜像文件头。为了
  支持EFI桩，应该用“MZ”魔术字符替换掉code0，并且res3（偏移量未0x3c）应指向PE/COFF
  文件头的其余部分.

- 表示文件头版本号的Drop-bit位域

	==========  ==========
	Bits 0:15   次要  版本
	Bits 16:31  主要  版本
	==========  ==========

  这保持了新旧版本之间的兼容性。
  当前版本被定义为0.2。

- 从版本0.2开始，结构体成员“magic”就已经被弃用，在之后的版本中，可能会移除掉它。
  最初，该成员应该与ARM64头的“magic”成员匹配，但遗憾的是并没有。
  “magic2”成员代替“magic”成员与ARM64头相匹配。

- 在当前的文件头，标志位域只剩下了一个位。

	=====  ==============================
	Bit 0  内核字节序。1 if BE, 0 if LE.
	=====  ==============================

- 对于引导加载程序加载内核映像来说，image_size成员对引导加载程序而言是必须的，否
  则将引导失败。
