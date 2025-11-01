.. SPDX-License-Identifier: GPL-2.0

=============================================
QTEE (Qualcomm Trusted Execution Environment)
=============================================

The QTEE driver handles communication with Qualcomm TEE [1].

The lowest level of communication with QTEE builds on the ARM SMC Calling
Convention (SMCCC) [2], which is the foundation for QTEE's Secure Channel
Manager (SCM) [3] used internally by the driver.

In a QTEE-based system, services are represented as objects with a series of
operations that can be called to produce results, including other objects.

When an object is hosted within QTEE, executing its operations is referred
to as "direct invocation". QTEE can also invoke objects hosted in the non-secure
world using a method known as "callback request".

The SCM provides two functions to support direct invocation and callback requests:

- QCOM_SCM_SMCINVOKE_INVOKE: Used for direct invocation. It can return either
  a result or initiate a callback request.
- QCOM_SCM_SMCINVOKE_CB_RSP: Used to submit a response to a callback request
  triggered by a previous direct invocation.

The QTEE Transport Message [4] is stacked on top of the SCM driver functions.

A message consists of two buffers shared with QTEE: inbound and outbound
buffers. The inbound buffer is used for direct invocation, and the outbound
buffer is used to make callback requests. This picture shows the contents of
a QTEE transport message::

                                      +---------------------+
                                      |                     v
    +-----------------+-------+-------+------+--------------------------+
    | qcomtee_msg_    |object | buffer       |                          |
    |  object_invoke  |  id   | offset, size |                          | (inbound buffer)
    +-----------------+-------+--------------+--------------------------+
    <---- header -----><---- arguments ------><- in/out buffer payload ->

                                      +-----------+
                                      |           v
    +-----------------+-------+-------+------+----------------------+
    | qcomtee_msg_    |object | buffer       |                      |
    |  callback       |  id   | offset, size |                      | (outbound buffer)
    +-----------------+-------+--------------+----------------------+

Each buffer is started with a header and array of arguments.

QTEE Transport Message supports four types of arguments:

- Input Object (IO) is an object parameter to the current invocation
  or callback request.
- Output Object (OO) is an object parameter from the current invocation
  or callback request.
- Input Buffer (IB) is (offset, size) pair to the inbound or outbound region
  to store parameter to the current invocation or callback request.
- Output Buffer (OB) is (offset, size) pair to the inbound or outbound region
  to store parameter from the current invocation or callback request.

Picture of the relationship between the different components in the QTEE
architecture::

         User space               Kernel                     Secure world
         ~~~~~~~~~~               ~~~~~~                     ~~~~~~~~~~~~
   +--------+   +----------+                                +--------------+
   | Client |   |callback  |                                | Trusted      |
   +--------+   |server    |                                | Application  |
      /\        +----------+                                +--------------+
      ||  +----------+ /\                                          /\
      ||  |callback  | ||                                          ||
      ||  |server    | ||                                          \/
      ||  +----------+ ||                                   +--------------+
      ||       /\      ||                                   | TEE Internal |
      ||       ||      ||                                   | API          |
      \/       \/      \/   +--------+--------+             +--------------+
   +---------------------+  | TEE    | QTEE   |             | QTEE         |
   |   libqcomtee [5]    |  | subsys | driver |             | Trusted OS   |
   +-------+-------------+--+----+-------+----+-------------+--------------+
   |      Generic TEE API        |       |   QTEE MSG                      |
   |      IOCTL (TEE_IOC_*)      |       |   SMCCC (QCOM_SCM_SMCINVOKE_*)  |
   +-----------------------------+       +---------------------------------+

References
==========

[1] https://docs.qualcomm.com/bundle/publicresource/topics/80-70015-11/qualcomm-trusted-execution-environment.html

[2] http://infocenter.arm.com/help/topic/com.arm.doc.den0028a/index.html

[3] drivers/firmware/qcom/qcom_scm.c

[4] drivers/tee/qcomtee/qcomtee_msg.h

[5] https://github.com/quic/quic-teec
