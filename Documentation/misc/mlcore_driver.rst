==============================
MLCore Kernel ML Driver Guide
==============================

Overview
--------

This document describes the MLCore kernel-space machine learning driver,
which supports:

- Hybrid kernel + user-space ML workflows
- IOCTL-based communication for inference and retraining
- /proc interface for result + training data observation
- History of predictions with timestamps
- Optional persistent logging to /var/log/mlcore.log

IOCTL Commands
--------------

The driver supports the following `ioctl()` commands:

- `IOCTL_SEND_INTS`: Send 3 fixed-point input features (int32)
- `IOCTL_SET_RESULT`: Set prediction result (int)
- `IOCTL_GET_RESULT`: Retrieve last prediction
- `IOCTL_ADD_TRAINING`: Add new training entry (features + label)
- `IOCTL_CLEAR_TRAINING`: Clear all training data

Procfs Interfaces
-----------------

- `/proc/mlcore_result` – current prediction + timestamped history
- `/proc/mlcore_train`  – list of all training entries

Device Node
-----------

The device can be accessed via:

.. code-block:: bash

    /dev/mlcore

Major number used: `104` (or dynamic via devtmpfs + udev rule)

Example Usage
-------------

From user space:

.. code-block:: python

    import fcntl, struct

    fd = open(\"/dev/mlcore\", \"wb\")
    features = [123, 456, 789]  # fixed-point (e.g., 1.23, 4.56)
    packed = struct.pack(\"3i\", *features)
    fcntl.ioctl(fd, 0x6800, packed)

See `scripts/train_data_sender.py` for more.

License
-------

This driver is licensed under GPL and authored by Sasikumar C.

