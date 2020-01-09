Linux Devlink Documentation
===========================

devlink is an API to expose device information and resources not directly
related to any device class, such as chip-wide/switch-ASIC-wide configuration.

Interface documentation
-----------------------

The following pages describe various interfaces available through devlink in
general.

.. toctree::
   :maxdepth: 1

   devlink-health
   devlink-info
   devlink-params
   devlink-trap
   devlink-trap-netdevsim

Driver-specific documentation
-----------------------------

Each driver that implements ``devlink`` is expected to document what
parameters, info versions, and other features it supports.

.. toctree::
   :maxdepth: 1

   bnxt
   mlx4
   mlx5
   mlxsw
   mv88e6xxx
   nfp
   ti-cpsw-switch
