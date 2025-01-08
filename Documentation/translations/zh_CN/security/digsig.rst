.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/security/digsig.rst

:翻译:
 赵硕 Shuo Zhao <zhaoshuo@cqsoftware.com.cn>

===============
数字签名验证API
===============

:作者: Dmitry Kasatkin
:日期: 2011.06.10


.. 内容

   1.介绍
   2.API
   3.用户空间工具


介绍
====

数字签名验证API提供了一种验证数字签名的方法。
目前，数字签名被IMA/EVM完整性保护子系统使用。

数字签名验证是通过精简的GnuPG多精度整数(MPI)库的内核移植来实现的。
该内核版本提供了内存分配错误处理，已根据内核编码风格进行重构，并修复
了checkpatch.pl报告的错误和警告。

公钥和签名由头部和MPIs组成::

	struct pubkey_hdr {
		uint8_t		version;	/* 密钥格式版本 */
		time_t		timestamp;	/* 密钥时间戳，目前为0 */
		uint8_t		algo;
		uint8_t		nmpi;
		char		mpi[0];
	} __packed;

	struct signature_hdr {
		uint8_t		version;	/* 签名格式版本 */
		time_t		timestamp;	/* 签名时间戳 */
		uint8_t		algo;
		uint8_t		hash;
		uint8_t		keyid[8];
		uint8_t		nmpi;
		char		mpi[0];
	} __packed;

keyid等同对整个密钥的内容进行SHA1哈希运算后的第12到19字节。
签名头部用于生成签名的输入。这种方法确保了密钥或签名头部无法更改。
它保护时间戳不被更改，并可以用于回滚保护。

API
===

目前API仅包含一个函数::

	digsig_verify() - 使用公钥进行数字签名验证

	/**
	* digsig_verify() - 使用公钥进行数字签名验证
	* @keyring:   查找密钥的密钥环
	* @sig:       数字签名
	* @sigen:     签名的长度
	* @data:      数据
	* @datalen:   数据的长度
	* @return:    成功时返回0，失败时返回 -EINVAL
	*
	* 验证数据相对于数字签名的完整性。
	* 目前仅支持RSA算法。
	* 通常将内容的哈希值作为此函数的数据。
	*
	*/
	int digsig_verify(struct key *keyring, const char *sig, int siglen,
				  const char *data, int datalen);

用户空间工具
============

签名和密钥管理实用工具evm-utils提供了生成签名、加载密钥到内核密钥环中的功能。
密钥可以是PEM格式，或转换为内核格式。
当把密钥添加到内核密钥环时，keyid定义该密钥的名称：下面的示例中为5D2B05FC633EE3E8。

以下是keyctl实用工具的示例输出::

	$ keyctl show
	Session Keyring
	-3 --alswrv      0     0  keyring: _ses
	603976250 --alswrv      0    -1   \_ keyring: _uid.0
	817777377 --alswrv      0     0       \_ user: kmk
	891974900 --alswrv      0     0       \_ encrypted: evm-key
	170323636 --alswrv      0     0       \_ keyring: _module
	548221616 --alswrv      0     0       \_ keyring: _ima
	128198054 --alswrv      0     0       \_ keyring: _evm

	$ keyctl list 128198054
	1 key in keyring:
	620789745: --alswrv     0     0 user: 5D2B05FC633EE3E8
