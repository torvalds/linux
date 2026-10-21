.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/usb/authorization.rst

:翻译:

 白钶凡 Kefan Bai <baikefan@leap-io-kernel.com>

:校译:


===========================
允许或禁止 USB 设备接入系统
===========================

版权 (C) 2007 Inaky Perez-Gonzalez
<inaky@linux.intel.com> 英特尔公司

有了这项功能，你就可以控制 USB 设备是否允许在系统中使用，并把 USB 设备锁
定机制完全放在用户空间实现。

目前，USB 设备一接入系统就会被立即配置，其接口也会立刻向用户开放。引入
这项机制后，只有在 root 明确授权后，设备才会完成配置并允许使用。


用法
====

授权设备接入系统::

	$ echo 1 > /sys/bus/usb/devices/DEVICE/authorized

取消设备授权::

	$ echo 0 > /sys/bus/usb/devices/DEVICE/authorized

将连接到 ``hostX`` 的新设备默认设为未授权（即锁定）::

	$ echo 0 > /sys/bus/usb/devices/usbX/authorized_default

解除锁定::

	$ echo 1 > /sys/bus/usb/devices/usbX/authorized_default

默认情况下，所有 USB 设备都是授权的。向 ``authorized_default`` 属性写入
``2`` 会使内核默认只授权连接到内部 USB 端口的设备。

系统锁定示例（简化版）
----------------------

假设你想做一个锁定机制，只允许 XYZ 类型的设备接入（例如一台带有外露 USB
端口的自助终端）::

  启动系统
  rc.local ->

   for host in /sys/bus/usb/devices/usb*
   do
      echo 0 > $host/authorized_default
   done

为 udev 配置一个脚本，用于处理新插入的 USB 设备::

 if device_is_my_type $DEV
 then
   echo 1 > $device_path/authorized
 fi


锁定方案是否可靠，关键全在 ``device_is_my_type()`` 的实现。仅仅检查
class、type 和 protocol 是否匹配，几乎是最差的一种安全校验方式；对想绕过
它的人来说，这种做法反而最容易伪造。如果你真要做安全控制，就该使用加密、
证书认证之类的机制。把 USB 存储设备当作“钥匙”的一个简单示例可以写成::

 function device_is_my_type()
 {
   echo 1 > authorized		# 暂时授权它
                                # FIXME: 确保没人能挂载它
   mount DEVICENODE /mntpoint
   sum=$(md5sum /mntpoint/.signature)
   if [ $sum = $(cat /etc/lockdown/keysum) ]
   then
        echo "验证通过，已连接"
        umount /mntpoint
        # 再做一些额外处理，让其他人也能使用它
   else
        echo 0 > authorized
   fi
 }


当然，这个例子仍然比较简化。真正落地时，更合适的做法是使用基于 PKI 的证
书校验，这样就不必依赖共享密钥之类的机制了。不过意思已经很清楚：任何拿到
设备仿真工具包的人，都能伪造描述符和设备信息，所以别把这类检查当成真正
的安全保障。

接口授权
--------

也可以用类似的方法允许或拒绝特定的 USB 接口。这样一来，你只需要阻止某个
USB 设备中的部分接口。

授权接口::

	$ echo 1 > /sys/bus/usb/devices/INTERFACE/authorized

取消接口授权::

	$ echo 0 > /sys/bus/usb/devices/INTERFACE/authorized

也可以更改特定 USB 总线上新接口的默认授权值。

默认允许接口::

	$ echo 1 > /sys/bus/usb/devices/usbX/interface_authorized_default

默认拒绝接口::

	$ echo 0 > /sys/bus/usb/devices/usbX/interface_authorized_default

默认情况下，``interface_authorized_default`` 位为 ``1``，因此所有接口默认
都会处于授权状态。

注意：
  如果把一个先前未授权的接口改为已授权，则必须通过将 ``INTERFACE`` 写入
  ``/sys/bus/usb/drivers_probe`` 来手动触发驱动探测。

对于需要多个接口的驱动程序，应先授权所有必需接口，然后再触发驱动探测。
这样做可以避免副作用。
