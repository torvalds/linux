.. SPDX-License-Identifier: GPL-2.0
..
.. Copyright 2019 Google LLC
..
.. c:namespace:: CEC

.. _CEC_ADAP_G_CONNECTOR_INFO:

*******************************
ioctl CEC_ADAP_G_CONNECTOR_INFO
*******************************

Name
====

CEC_ADAP_G_CONNECTOR_INFO - Query HDMI connector information

Synopsis
========

.. c:macro:: CEC_ADAP_G_CONNECTOR_INFO

``int ioctl(int fd, CEC_ADAP_G_CONNECTOR_INFO, struct cec_connector_info *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``

Description
===========

Using this ioctl an application can learn which HDMI connector this CEC
device corresponds to. While calling this ioctl the application should
provide a pointer to a cec_connector_info struct which will be populated
by the kernel with the info provided by the adapter's driver. This ioctl
is only available if the ``CEC_CAP_CONNECTOR_INFO`` capability is set.

.. tabularcolumns:: |p{1.0cm}|p{4.4cm}|p{2.5cm}|p{9.6cm}|

.. c:type:: cec_connector_info

.. flat-table:: struct cec_connector_info
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 8

    * - __u32
      - ``type``
      - The type of connector this adapter is associated with.
    * - union {
      - ``(anonymous)``
    * - ``struct cec_drm_connector_info``
      - drm
      - :ref:`cec-drm-connector-info`
    * - }
      -

.. tabularcolumns:: |p{4.4cm}|p{2.5cm}|p{10.6cm}|

.. _connector-type:

.. flat-table:: Connector types
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 8

    * .. _`CEC-CONNECTOR-TYPE-NO-CONNECTOR`:

      - ``CEC_CONNECTOR_TYPE_NO_CONNECTOR``
      - 0
      - No connector is associated with the adapter/the information is not
        provided by the driver.
    * .. _`CEC-CONNECTOR-TYPE-DRM`:

      - ``CEC_CONNECTOR_TYPE_DRM``
      - 1
      - Indicates that a DRM connector is associated with this adapter.
        Information about the connector can be found in
	:ref:`cec-drm-connector-info`.

.. tabularcolumns:: |p{4.4cm}|p{2.5cm}|p{10.6cm}|

.. c:type:: cec_drm_connector_info

.. _cec-drm-connector-info:

.. flat-table:: struct cec_drm_connector_info
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 8

    * .. _`CEC-DRM-CONNECTOR-TYPE-CARD-NO`:

      - __u32
      - ``card_no``
      - DRM card number: the number from a card's path, e.g. 0 in case of
        /dev/card0.
    * .. _`CEC-DRM-CONNECTOR-TYPE-CONNECTOR_ID`:

      - __u32
      - ``connector_id``
      - DRM connector ID.
