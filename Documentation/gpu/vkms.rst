.. _vkms:

==========================================
 drm/vkms Virtual Kernel Modesetting
==========================================

.. kernel-doc:: drivers/gpu/drm/vkms/vkms_drv.c
   :doc: vkms (Virtual Kernel Modesetting)

TODO
====

CRC API
-------

- Optimize CRC computation ``compute_crc()`` and plane blending ``blend()``

- Use the alpha value to blend vaddr_src with vaddr_dst instead of
  overwriting it in ``blend()``.

- Add igt test to check cleared alpha value for XRGB plane format.

- Add igt test to check extreme alpha values i.e. fully opaque and fully
  transparent (intermediate values are affected by hw-specific rounding modes).
