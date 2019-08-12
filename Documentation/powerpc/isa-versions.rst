:orphan:

CPU to ISA Version Mapping
==========================

Mapping of some CPU versions to relevant ISA versions.

========= ====================
CPU       Architecture version
========= ====================
Power9    Power ISA v3.0B
Power8    Power ISA v2.07
Power7    Power ISA v2.06
Power6    Power ISA v2.05
PA6T      Power ISA v2.04
Cell PPU  - Power ISA v2.02 with some minor exceptions
          - Plus Altivec/VMX ~= 2.03
Power5++  Power ISA v2.04 (no VMX)
Power5+   Power ISA v2.03
Power5    - PowerPC User Instruction Set Architecture Book I v2.02
          - PowerPC Virtual Environment Architecture Book II v2.02
          - PowerPC Operating Environment Architecture Book III v2.02
PPC970    - PowerPC User Instruction Set Architecture Book I v2.01
          - PowerPC Virtual Environment Architecture Book II v2.01
          - PowerPC Operating Environment Architecture Book III v2.01
          - Plus Altivec/VMX ~= 2.03
========= ====================


Key Features
------------

========== ==================
CPU        VMX (aka. Altivec)
========== ==================
Power9     Yes
Power8     Yes
Power7     Yes
Power6     Yes
PA6T       Yes
Cell PPU   Yes
Power5++   No
Power5+    No
Power5     No
PPC970     Yes
========== ==================

========== ====
CPU        VSX
========== ====
Power9     Yes
Power8     Yes
Power7     Yes
Power6     No
PA6T       No
Cell PPU   No
Power5++   No
Power5+    No
Power5     No
PPC970     No
========== ====

========== ====================
CPU        Transactional Memory
========== ====================
Power9     Yes (* see transactional_memory.txt)
Power8     Yes
Power7     No
Power6     No
PA6T       No
Cell PPU   No
Power5++   No
Power5+    No
Power5     No
PPC970     No
========== ====================
