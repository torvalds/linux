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

   devlink-dpipe
   devlink-health
   devlink-info
   devlink-flash
   devlink-params
   devlink-region
   devlink-resource
   devlink-trap

Driver-specific documentation
-----------------------------

Each driver that implements ``devlink`` is expected to document what
parameters, info versions, and other features it supports.

.. toctree::
   :maxdepth: 1

   bnxt
   ionic
   ice
   mlx4
   mlx5
   mlxsw
   mv88e6xxx
   netdevsim
   nfp
   qed
   ti-cpsw-switch
