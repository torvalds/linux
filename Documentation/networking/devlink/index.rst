Linux Devlink Documentation
===========================

devlink is an API to expose device information and resources not directly
related to any device class, such as chip-wide/switch-ASIC-wide configuration.

Locking
-------

Driver facing APIs are currently transitioning to allow more explicit
locking. Drivers can use the existing ``devlink_*`` set of APIs, or
new APIs prefixed by ``devl_*``. The older APIs handle all the locking
in devlink core, but don't allow registration of most sub-objects once
the main devlink object is itself registered. The newer ``devl_*`` APIs assume
the devlink instance lock is already held. Drivers can take the instance
lock by calling ``devl_lock()``. It is also held all callbacks of devlink
netlink commands.

Drivers are encouraged to use the devlink instance lock for their own needs.

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
   devlink-port
   devlink-region
   devlink-resource
   devlink-reload
   devlink-selftests
   devlink-trap
   devlink-linecard

Driver-specific documentation
-----------------------------

Each driver that implements ``devlink`` is expected to document what
parameters, info versions, and other features it supports.

.. toctree::
   :maxdepth: 1

   bnxt
   hns3
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
   am65-nuss-cpsw-switch
   prestera
   iosm
   octeontx2
