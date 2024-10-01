==========================
CPU to ISA Version Mapping
==========================

Mapping of some CPU versions to relevant ISA versions.

Note Power4 and Power4+ are not supported.

========= ====================================================================
CPU       Architecture version
========= ====================================================================
Power10   Power ISA v3.1
Power9    Power ISA v3.0B
Power8    Power ISA v2.07
e6500     Power ISA v2.06 with some exceptions
e5500     Power ISA v2.06 with some exceptions, no Altivec
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
Power4+   - PowerPC User Instruction Set Architecture Book I v2.01
          - PowerPC Virtual Environment Architecture Book II v2.01
          - PowerPC Operating Environment Architecture Book III v2.01
Power4    - PowerPC User Instruction Set Architecture Book I v2.00
          - PowerPC Virtual Environment Architecture Book II v2.00
          - PowerPC Operating Environment Architecture Book III v2.00
========= ====================================================================


Key Features
------------

========== ==================
CPU        VMX (aka. Altivec)
========== ==================
Power10    Yes
Power9     Yes
Power8     Yes
e6500      Yes
e5500      No
Power7     Yes
Power6     Yes
PA6T       Yes
Cell PPU   Yes
Power5++   No
Power5+    No
Power5     No
PPC970     Yes
Power4+    No
Power4     No
========== ==================

========== ====
CPU        VSX
========== ====
Power10    Yes
Power9     Yes
Power8     Yes
e6500      No
e5500      No
Power7     Yes
Power6     No
PA6T       No
Cell PPU   No
Power5++   No
Power5+    No
Power5     No
PPC970     No
Power4+    No
Power4     No
========== ====

========== ====================================
CPU        Transactional Memory
========== ====================================
Power10    No  (* see Power ISA v3.1, "Appendix A. Notes on the Removal of Transactional Memory from the Architecture")
Power9     Yes (* see transactional_memory.txt)
Power8     Yes
e6500      No
e5500      No
Power7     No
Power6     No
PA6T       No
Cell PPU   No
Power5++   No
Power5+    No
Power5     No
PPC970     No
Power4+    No
Power4     No
========== ====================================
