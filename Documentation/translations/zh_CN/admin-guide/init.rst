.. include:: ../disclaimer-zh_CN.rst

:Original: :doc:`../../../admin-guide/init`

:译者:

 吴想成 Wu XiangCheng <bobwxc@email.cn>

解释“No working init found.”启动挂起消息
=========================================

:作者:

 Andreas Mohr <andi at lisas period de>

 Cristian Souza <cristianmsbr at gmail period com>

本文档提供了加载初始化二进制（init binary）失败的一些高层级原因（大致按执行
顺序列出）。

1) **无法挂载根文件系统Unable to mount root FS** ：请设置“debug”内核参数（在
   引导加载程序bootloader配置文件或CONFIG_CMDLINE）以获取更详细的内核消息。

2) **初始化二进制不存在于根文件系统上init binary doesn't exist on rootfs** ：
   确保您的根文件系统类型正确（并且 ``root=`` 内核参数指向正确的分区）；拥有
   所需的驱动程序，例如SCSI或USB等存储硬件；文件系统（ext3、jffs2等）是内建的
   （或者作为模块由initrd预加载）。

3) **控制台设备损坏Broken console device** ： ``console= setup`` 中可能存在
   冲突 --> 初始控制台不可用（initial console unavailable）。例如，由于串行
   IRQ问题（如缺少基于中断的配置）导致的某些串行控制台不可靠。尝试使用不同的
   ``console= device`` 或像 ``netconsole=`` 。

4) **二进制存在但依赖项不可用Binary exists but dependencies not available** ：
   例如初始化二进制的必需库依赖项，像 ``/lib/ld-linux.so.2`` 丢失或损坏。使用
   ``readelf -d <INIT>|grep NEEDED`` 找出需要哪些库。

5) **无法加载二进制Binary cannot be loaded** ：请确保二进制的体系结构与您的
   硬件匹配。例如i386不匹配x86_64，或者尝试在ARM硬件上加载x86。如果您尝试在
   此处加载非二进制文件（shell脚本？），您应该确保脚本在其工作头（shebang
   header）行 ``#!/...`` 中指定能正常工作的解释器（包括其库依赖项）。在处理
   脚本之前，最好先测试一个简单的非脚本二进制文件，比如 ``/bin/sh`` ，并确认
   它能成功执行。要了解更多信息，请将代码添加到 ``init/main.c`` 以显示
   kernel_execve()的返回值。

当您发现新的失败原因时，请扩展本解释（毕竟加载初始化二进制是一个 **关键** 且
艰难的过渡步骤，需要尽可能无痛地进行），然后向LKML提交一个补丁。

待办事项：

- 通过一个可以存储 ``kernel_execve()`` 结果值的结构体数组实现各种
  ``run_init_process()`` 调用，并在失败时通过迭代 **所有** 结果来记录一切
  （非常重要的可用性修复）。
- 试着使实现本身在一般情况下更有帮助，例如在受影响的地方提供额外的错误消息。
