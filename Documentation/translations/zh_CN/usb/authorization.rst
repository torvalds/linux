.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/usb/authorization.rst

:翻译:

 白钶凡 Kefan Bai <baikefan@leap-io-kernel.com>

:校译:


=============================
授权或禁止 USB 设备连接到系统
=============================

版权 (C) 2007 Inaky Perez-Gonzalez
<inaky@linux.intel.com> 英特尔公司

此功能允许你控制 USB 设备是否可以在系统中使用。
借助它，你可以完全通过用户空间实现对 USB 设备的锁定。

目前，当插入一个 USB 设备时，系统会对其进行配置，
其接口会立即向用户开放。
有了这项改动，只有在 root 授权设备完成配置后，
设备才可被使用。


用法
====

授权设备接入::

	$ echo 1 > /sys/bus/usb/devices/DEVICE/authorized

取消对设备的授权::

	$ echo 0 > /sys/bus/usb/devices/DEVICE/authorized

将新连接到 ``hostX`` 的设备默认设为未授权（即锁定）::

	$ echo 0 > /sys/bus/usb/devices/usbX/authorized_default

解除锁定::

	$ echo 1 > /sys/bus/usb/devices/usbX/authorized_default

默认情况下，所有 USB 设备都是授权的。
向 ``authorized_default`` 属性写入 ``2`` 会使内核
默认只授权连接到内部 USB 端口的设备。

系统锁定示例（比较粗糙）
------------------------

假设你想实现一个锁定功能，只允许类型为 XYZ 的设备接入
（例如某台带有外露 USB 端口的自助服务终端）::

  启动系统
  rc.local ->

   for host in /sys/bus/usb/devices/usb*
   do
      echo 0 > $host/authorized_default
   done

给 udev 挂一个脚本，用于处理新插入的 USB 设备::

 if device_is_my_type $DEV
 then
   echo 1 > $device_path/authorized
 done


``device_is_my_type()`` 才是锁定方案真正见功夫的
地方。仅仅检查 class、type 和 protocol 是否匹配
某个值，是你能做出的最糟糕的安全验证之一；
对想绕过它的人来说，这反而是最容易利用的方案。
如果你需要真正安全的办法，那就该使用加密、
证书认证之类的机制。把 USB 存储设备当作
“钥匙”的一个简单例子可以是::

 function device_is_my_type()
 {
   echo 1 > authorized		# 暂时授权它
                                # FIXME: 确保没人能挂载它
   mount DEVICENODE /mntpoint
   sum=$(md5sum /mntpoint/.signature)
   if [ $sum = $(cat /etc/lockdown/keysum) ]
   then
        echo "We are good, connected"
        umount /mntpoint
        # 再做一些额外处理，让其他人也能使用它
   else
        echo 0 > authorized
   fi
 }


当然，这个例子很粗糙；真正要做的话，
你会想用基于 PKI 的证书校验，这样就不必依赖
共享密钥之类的东西。不过你应该已经明白意思了。
任何拿到设备仿真工具包的人都能伪造描述符和设备信息。
别信这个。

接口授权
--------

也有类似的方法用于允许或拒绝特定 USB 接口。
这使得你可以只阻止某个 USB 设备中的部分接口。

授权接口::

	$ echo 1 > /sys/bus/usb/devices/INTERFACE/authorized

取消接口授权::

	$ echo 0 > /sys/bus/usb/devices/INTERFACE/authorized

也可以更改特定 USB 总线上新接口的默认授权值。

默认允许接口::

	$ echo 1 > /sys/bus/usb/devices/usbX/interface_authorized_default

默认拒绝接口::

	$ echo 0 > /sys/bus/usb/devices/usbX/interface_authorized_default

默认情况下，
``interface_authorized_default`` 位为 ``1``，
因此所有接口默认都处于已授权状态。

注意：
  如果把一个先前未授权的接口改为已授权，
  则必须通过将 ``INTERFACE`` 写入 ``/sys/bus/usb/drivers_probe``
  来手动触发驱动探测。

对于需要多个接口的驱动程序，应先授权所有必需接口，
然后再触发驱动探测。这样做可以避免副作用。
