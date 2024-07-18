.. include:: ../../disclaimer-ita.rst

:Original: :doc:`../../../../arch/riscv/patch-acceptance`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

arch/riscv linee guida alla manutenzione per gli sviluppatori
=============================================================

Introduzione
------------

L'insieme di istruzioni RISC-V sono sviluppate in modo aperto: le
bozze in fase di sviluppo sono disponibili a tutti per essere
revisionate e per essere sperimentare nelle implementazioni.  Le bozze
dei nuovi moduli o estensioni possono cambiare in fase di sviluppo - a
volte in modo incompatibile rispetto a bozze precedenti.  Questa
flessibilità può portare a dei problemi di manutenzioni per il
supporto RISC-V nel kernel Linux. I manutentori Linux non amano
l'abbandono del codice, e il processo di sviluppo del kernel
preferisce codice ben revisionato e testato rispetto a quello
sperimentale.  Desideriamo estendere questi stessi principi al codice
relativo all'architettura RISC-V che verrà accettato per l'inclusione
nel kernel.

Patchwork
---------

RISC-V ha un'istanza di patchwork dov'è possibile controllare lo stato delle patch:

  https://patchwork.kernel.org/project/linux-riscv/list/

Se la vostra patch non appare nella vista predefinita, i manutentori di RISC-V
hanno probabilmente richiesto delle modifiche o si aspettano che venga applicata
a un altro albero.

Il processo automatico viene eseguito su questa istanza di patchwork, costruendo
e collaudando le patch man mano che arrivano. Il processo applica le patch al
riferimento HEAD corrente dei rami `for-next` e `fixes` dei sorgenti RISC-V,
questo a seconda che la patch sia stata o meno individuata come correzione. In
caso contrario, utilizzerà il ramo `master` di RISC-V. L'esatto commit a cui è
stata applicata una serie di patch sarà annotato su patchwork. È improbabile che
vengano applicate Le patch che non passano i controlli, nella maggior parte dei
casi dovranno essere ripresentate.

In aggiunta alla lista delle verifiche da fare prima di inviare una patch
-------------------------------------------------------------------------

Accetteremo le patch per un nuovo modulo o estensione se la fondazione
RISC-V li classifica come "Frozen" o "Retified".  (Ovviamente, gli
sviluppatori sono liberi di mantenere una copia del kernel Linux
contenente il codice per una bozza di estensione).

In aggiunta, la specifica RISC-V permette agli implementatori di
creare le proprie estensioni.  Queste estensioni non passano
attraverso il processo di revisione della fondazione RISC-V.  Per
questo motivo, al fine di evitare complicazioni o problemi di
prestazioni, accetteremo patch solo per quelle estensioni che sono
state ufficialmente accettate dalla fondazione RISC-V.  (Ovviamente,
gli implementatori sono liberi di mantenere una copia del kernel Linux
contenente il codice per queste specifiche estensioni).
