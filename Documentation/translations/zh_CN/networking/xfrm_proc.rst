.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/xfrm_proc.rst

:翻译:

   王亚鑫 Wang Yaxin <wang.yaxin@zte.com.cn>

=================================
XFRM proc - /proc/net/xfrm_* 文件
=================================

作者：Masahide NAKAMURA <nakam@linux-ipv6.org>


转换统计信息
------------

`xfrm_proc` 提供一组统计计数器，显示转换过程中丢弃的数据包及其原因。
这些计数器属于Linux私有MIB的一部分，可通过 `/proc/net/xfrm_stat`
查看。

入站错误
~~~~~~~~

XfrmInError:
	未匹配其他类别的所有错误

XfrmInBufferError:
	缓冲区不足

XfrmInHdrError:
	头部错误

XfrmInNoStates:
	未找到状态
	（入站SPI、地址或SA的IPsec协议不匹配）

XfrmInStateProtoError:
	转换协议相关的错误
	（如SA密钥错误）

XfrmInStateModeError:
	转换模式相关的错误

XfrmInStateSeqError:
    序列号错误
	序列号超出窗口范围

XfrmInStateExpired:
	状态已过期

XfrmInStateMismatch:
	状态选项不匹配
	（如UDP封装类型不匹配）

XfrmInStateInvalid:
	无效状态

XfrmInTmplMismatch:
	状态模板不匹配
	（如入站SA正确但SP规则错误）

XfrmInNoPols:
	未找到状态的对应策略
	（如入站SA正确但无SP规则）

XfrmInPolBlock:
	丢弃的策略

XfrmInPolError:
	错误的策略

XfrmAcquireError:
	状态未完全获取即被使用

XfrmFwdHdrError:
	转发路由禁止

XfrmInStateDirError:
	状态方向不匹配
	（输入路径查找到输出状态，预期是输入状态或者无方向）

出站错误
~~~~~~~~
XfrmOutError:
	未匹配其他类别的所有错误

XfrmOutBundleGenError:
	捆绑包生成错误

XfrmOutBundleCheckError:
	捆绑包校验错误

XfrmOutNoStates:
	未找到状态

XfrmOutStateProtoError:
	转换协议特定错误

XfrmOutStateModeError:
	转换模式特定错误

XfrmOutStateSeqError:
	序列号错误
	（序列号溢出）

XfrmOutStateExpired:
	状态已过期

XfrmOutPolBlock:
	丢弃策略

XfrmOutPolDead:
	失效策略

XfrmOutPolError:
	错误策略

XfrmOutStateInvalid:
	无效状态（可能已过期）

XfrmOutStateDirError:
	状态方向不匹配（输出路径查找到输入状态，预期为输出状态或无方向）
