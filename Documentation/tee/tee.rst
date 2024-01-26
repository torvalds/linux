.. SPDX-License-Identifier: GPL-2.0

===================================
TEE (Trusted Execution Environment)
===================================

This document describes the TEE subsystem in Linux.

Overview
========

A TEE is a trusted OS running in some secure environment, for example,
TrustZone on ARM CPUs, or a separate secure co-processor etc. A TEE driver
handles the details needed to communicate with the TEE.

This subsystem deals with:

- Registration of TEE drivers

- Managing shared memory between Linux and the TEE

- Providing a generic API to the TEE
