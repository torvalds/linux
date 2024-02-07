.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-ita.rst

=============================================
Le operazioni RCU per le verifiche *torture*
=============================================

CONFIG_RCU_TORTURE_TEST
=======================

L'opzione CONFIG_RCU_TORTURE_TEST è disponibile per tutte le implementazione di
RCU. L'opzione creerà un modulo rcutorture che potrete caricare per avviare le
verifiche. La verifica userà printk() per riportare lo stato, dunque potrete
visualizzarlo con dmesg (magari usate grep per filtrare "torture"). Le verifiche
inizieranno al caricamento, e si fermeranno alla sua rimozione.

I parametri di modulo hanno tutti il prefisso "rcutortute.", vedere
Documentation/admin-guide/kernel-parameters.txt.

Rapporto
========

Il rapporto sulle verifiche si presenta nel seguente modo::

	rcu-torture:--- Start of test: nreaders=16 nfakewriters=4 stat_interval=30 verbose=0 test_no_idle_hz=1 shuffle_interval=3 stutter=5 irqreader=1 fqs_duration=0 fqs_holdoff=0 fqs_stutter=3 test_boost=1/0 test_boost_interval=7 test_boost_duration=4
	rcu-torture: rtc:           (null) ver: 155441 tfle: 0 rta: 155441 rtaf: 8884 rtf: 155440 rtmbe: 0 rtbe: 0 rtbke: 0 rtbre: 0 rtbf: 0 rtb: 0 nt: 3055767
	rcu-torture: Reader Pipe:  727860534 34213 0 0 0 0 0 0 0 0 0
	rcu-torture: Reader Batch:  727877838 17003 0 0 0 0 0 0 0 0 0
	rcu-torture: Free-Block Circulation:  155440 155440 155440 155440 155440 155440 155440 155440 155440 155440 0
	rcu-torture:--- End of test: SUCCESS: nreaders=16 nfakewriters=4 stat_interval=30 verbose=0 test_no_idle_hz=1 shuffle_interval=3 stutter=5 irqreader=1 fqs_duration=0 fqs_holdoff=0 fqs_stutter=3 test_boost=1/0 test_boost_interval=7 test_boost_duration=4

Sulla maggior parte dei sistemi questo rapporto si produce col comando "dmesg |
grep torture:". Su configurazioni più esoteriche potrebbe essere necessario
usare altri comandi per visualizzare i messaggi di printk(). La funzione
printk() usa KERN_ALERT, dunque i messaggi dovrebbero essere ben visibili. ;-)

La prima e l'ultima riga mostrano i parametri di module di rcutorture, e solo
sull'ultima riga abbiamo il risultato finale delle verifiche effettuate che può
essere "SUCCESS" (successo) or "FAILURE" (insuccesso).

Le voci sono le seguenti:

* "rtc": L'indirizzo in esadecimale della struttura attualmente visibile dai
   lettori.

* "ver": Il numero di volte dall'avvio che il processo scrittore di RCU ha
  cambiato la struttura visible ai lettori.

* "tfle": se non è zero, indica la lista di strutture "torture freelist" da
  mettere in "rtc" è vuota. Questa condizione è importante perché potrebbe
  illuderti che RCU stia funzionando mentre invece non è il caso. :-/

* "rta": numero di strutture allocate dalla lista "torture freelist".

* "rtaf": il numero di allocazioni fallite dalla lista "torture freelist" a
  causa del fatto che fosse vuota. Non è inusuale che sia diverso da zero, ma è
  un brutto segno se questo numero rappresenta una frazione troppo alta di
  "rta".

* "rtf": il numero di rilasci nella lista "torture freelist"

* "rtmbe": Un valore diverso da zero indica che rcutorture crede che
  rcu_assign_pointer() e rcu_dereference() non funzionino correttamente. Il
  valore dovrebbe essere zero.

* "rtbe": un valore diverso da zero indica che le funzioni della famiglia
  rcu_barrier() non funzionano correttamente.

* "rtbke": rcutorture è stato capace di creare dei kthread real-time per forzare
  l'inversione di priorità di RCU. Il valore dovrebbe essere zero.

* "rtbre": sebbene rcutorture sia riuscito a creare dei kthread capaci di
  forzare l'inversione di priorità, non è riuscito però ad impostarne la
  priorità real-time al livello 1. Il valore dovrebbe essere zero.

* "rtbf": Il numero di volte che è fallita la promozione della priorità per
  risolvere un'inversione.

* "rtb": Il numero di volte che rcutorture ha provato a forzare l'inversione di
  priorità. Il valore dovrebbe essere diverso da zero Se state verificando la
  promozione della priorità col parametro "test_bootst".

* "nt": il numero di volte che rcutorture ha eseguito codice lato lettura
  all'interno di un gestore di *timer*. Questo valore dovrebbe essere diverso da
  zero se avete specificato il parametro "irqreader".

* "Reader Pipe": un istogramma dell'età delle strutture viste dai lettori. RCU
  non funziona correttamente se una qualunque voce, dalla terza in poi, ha un
  valore diverso da zero. Se dovesse succedere, rcutorture stampa la stringa
  "!!!" per renderlo ben visibile. L'età di una struttura appena creata è zero,
  diventerà uno quando sparisce dalla visibilità di un lettore, e incrementata
  successivamente per ogni periodo di grazia; infine rilasciata dopo essere
  passata per (RCU_TORTURE_PIPE_LEN-2) periodi di grazia.

  L'istantanea qui sopra è stata presa da una corretta implementazione di RCU.
  Se volete vedere come appare quando non funziona, sbizzarritevi nel romperla.
  ;-)

* "Reader Batch": un istogramma di età di strutture viste dai lettori, ma
  conteggiata in termini di lotti piuttosto che periodi. Anche qui dalla terza
  voce in poi devono essere zero. La ragione d'esistere di questo rapporto è che
  a volte è più facile scatenare un terzo valore diverso da zero qui piuttosto
  che nella lista "Reader Pipe".

* "Free-Block Circulation": il numero di strutture *torture* che hanno raggiunto
  un certo punto nella catena. Il primo numero dovrebbe corrispondere
  strettamente al numero di strutture allocate; il secondo conta quelle rimosse
  dalla vista dei lettori. Ad eccezione dell'ultimo valore, gli altri
  corrispondono al numero di passaggi attraverso il periodo di grazia. L'ultimo
  valore dovrebbe essere zero, perché viene incrementato solo se il contatore
  della struttura torture viene in un qualche modo incrementato oltre il
  normale.

Una diversa implementazione di RCU potrebbe fornire informazioni aggiuntive. Per
esempio, *Tree SRCU* fornisce anche la seguente riga::

	srcud-torture: Tree SRCU per-CPU(idx=0): 0(35,-21) 1(-4,24) 2(1,1) 3(-26,20) 4(28,-47) 5(-9,4) 6(-10,14) 7(-14,11) T(1,6)

Questa riga mostra lo stato dei contatori per processore, in questo caso per
*Tree SRCU*, usando un'allocazione dinamica di srcu_struct (dunque "srcud-"
piuttosto che "srcu-"). I numeri fra parentesi sono i valori del "vecchio"
contatore e di quello "corrente" per ogni processore. Il valore "idx" mappa
questi due valori nell'array, ed è utile per il *debug*. La "T" finale contiene
il valore totale dei contatori.

Uso su specifici kernel
=======================

A volte può essere utile eseguire RCU torture su un kernel già compilato, ad
esempio quando lo si sta per mettere in proeduzione. In questo caso, il kernel
dev'essere compilato con CONFIG_RCU_TORTURE_TEST=m, cosicché le verifiche possano
essere avviate usano modprobe e terminate con rmmod.

Per esempio, potreste usare questo script::

	#!/bin/sh

	modprobe rcutorture
	sleep 3600
	rmmod rcutorture
	dmesg | grep torture:

Potete controllare il rapporto verificando manualmente la presenza del marcatore
di errore "!!!". Ovviamente, siete liberi di scriverne uno più elaborato che
identifichi automaticamente gli errori. Il comando "rmmod" forza la stampa di
"SUCCESS" (successo), "FAILURE" (fallimento), o "RCU_HOTPLUG". I primi due sono
autoesplicativi; invece, l'ultimo indica che non son stati trovati problemi in
RCU, tuttavia ci sono stati problemi con CPU-hotplug.


Uso sul kernel di riferimento
=============================

Quando si usa rcutorture per verificare modifiche ad RCU stesso, spesso è
necessario compilare un certo numero di kernel usando configurazioni diverse e
con parametri d'avvio diversi. In questi casi, usare modprobe ed rmmod potrebbe
richiedere molto tempo ed il processo essere suscettibile ad errori.

Dunque, viene messo a disposizione il programma
tools/testing/selftests/rcutorture/bin/kvm.sh per le architetture x86, arm64 e
powerpc. Di base, eseguirà la serie di verifiche elencate in
tools/testing/selftests/rcutorture/configs/rcu/CFLIST. Ognuna di queste verrà
eseguita per 30 minuti in una macchina virtuale con uno spazio utente minimale
fornito da un initrd generato automaticamente. Al completamento, gli artefatti
prodotti e i messaggi vengono analizzati alla ricerca di errori, ed i risultati
delle esecuzioni riassunti in un rapporto.

Su grandi sistemi, le verifiche di rcutorture posso essere velocizzare passano a
kvm.sh l'argomento --cpus. Per esempio, su un sistema a 64 processori, "--cpus
43" userà fino a 43 processori per eseguire contemporaneamente le verifiche. Su
un kernel v5.4 per eseguire tutti gli scenari in due serie, riduce il tempo
d'esecuzione da otto ore a un'ora (senza contare il tempo per compilare sedici
kernel). L'argomento "--dryrun sched" non eseguirà verifiche, piuttosto vi
informerà su come queste verranno organizzate in serie. Questo può essere utile
per capire quanti processori riservare per le verifiche in --cpus.

Non serve eseguire tutti gli scenari di verifica per ogni modifica. Per esempio,
per una modifica a Tree SRCU potete eseguire gli scenari SRCU-N e SRCU-P. Per
farlo usate l'argomento --configs di kvm.sh in questo modo: "--configs 'SRCU-N
SRCU-P'". Su grandi sistemi si possono eseguire più copie degli stessi scenari,
per esempio, un hardware che permette di eseguire 448 thread, può eseguire 5
istanze complete contemporaneamente. Per farlo::

	kvm.sh --cpus 448 --configs '5*CFLIST'

Oppure, lo stesso sistema, può eseguire contemporaneamente 56 istanze dello
scenario su otto processori::

	kvm.sh --cpus 448 --configs '56*TREE04'

O ancora 28 istanze per ogni scenario su otto processori::

	kvm.sh --cpus 448 --configs '28*TREE03 28*TREE04'

Ovviamente, ogni esecuzione utilizzerà della memoria. Potete limitarne l'uso con
l'argomento --memory, che di base assume il valore 512M. Per poter usare valori
piccoli dovrete disabilitare le verifiche *callback-flooding* usando il
parametro --bootargs che vedremo in seguito.

A volte è utile avere informazioni aggiuntive di debug, in questo caso potete
usare il parametro --kconfig, per esempio, ``--kconfig
'CONFIG_RCU_EQS_DEBUG=y'``. In aggiunta, ci sono i parametri --gdb, --kasan, and
kcsan. Da notare che --gdb vi limiterà all'uso di un solo scenario per
esecuzione di kvm.sh e richiede di avere anche un'altra finestra aperta dalla
quale eseguire ``gdb`` come viene spiegato dal programma.

Potete passare anche i parametri d'avvio del kernel, per esempio, per
controllare i parametri del modulo rcutorture. Per esempio, per verificare
modifiche del codice RCU CPU stall-warning, usate ``bootargs
'rcutorture.stall_cpu=30``. Il programma riporterà un fallimento, ossia il
risultato della verifica. Come visto in precedenza, ridurre la memoria richiede
la disabilitazione delle verifiche *callback-flooding*::

	kvm.sh --cpus 448 --configs '56*TREE04' --memory 128M \
		--bootargs 'rcutorture.fwd_progress=0'

A volte tutto quello che serve è una serie completa di compilazioni del kernel.
Questo si ottiene col parametro --buildonly.

Il parametro --duration sovrascrive quello di base di 30 minuti. Per esempio,
con ``--duration 2d`` l'esecuzione sarà di due giorni, ``--duraction 5min`` di
cinque minuti, e ``--duration 45s`` di 45 secondi. L'ultimo può essere utile per
scovare rari errori nella sequenza d'avvio.

Infine, il parametro --trust-make permette ad ogni nuova compilazione del kernel
di riutilizzare tutto il possibile da quelle precedenti. Da notare che senza il
parametro --trust-make, i vostri file di *tag* potrebbero essere distrutti.

Ci sono altri parametri più misteriosi che sono documentati nel codice sorgente
dello programma kvm.sh.

Se un'esecuzione contiene degli errori, il loro numero durante la compilazione e
all'esecuzione verranno elencati alla fine fra i risultati di kvm.sh (che vi
consigliamo caldamente di reindirizzare verso un file). I file prodotti dalla
compilazione ed i risultati stampati vengono salvati, usando un riferimento
temporale, nelle cartella tools/testing/selftests/rcutorture/res. Una cartella
di queste cartelle può essere fornita a kvm-find-errors.sh per estrarne gli
errori. Per esempio::

	tools/testing/selftests/rcutorture/bin/kvm-find-errors.sh \
		tools/testing/selftests/rcutorture/res/2020.01.20-15.54.23

Tuttavia, molto spesso è più conveniente aprire i file direttamente. I file
riguardanti tutti gli scenari di un'esecuzione di trovano nella cartella
principale (2020.01.20-15.54.23 nell'esempio precedente), mentre quelli
specifici per scenario si trovano in sotto cartelle che prendono il nome dello
scenario stesso (per esempio, "TREE04"). Se un dato scenario viene eseguito più
di una volta (come abbiamo visto con "--configs '56*TREE04'"), allora dalla
seconda esecuzione in poi le sottocartelle includeranno un numero di
progressione, per esempio "TREE04.2", "TREE04.3", e via dicendo.

Il file solitamente più usato nella cartella principale è testid.txt. Se la
verifica viene eseguita in un repositorio git, allora questo file conterrà il
*commit* sul quale si basano le verifiche, mentre tutte le modifiche non
registrare verranno mostrate in formato diff.

I file solitamente più usati nelle cartelle di scenario sono:

.config
  Questo file contiene le opzioni di Kconfig

Make.out
  Questo file contiene il risultato di compilazione per uno specifico scenario

console.log
  Questo file contiene il risultato d'esecuzione per uno specifico scenario.
  Questo file può essere esaminato una volta che il kernel è stato avviato,
  ma potrebbe non esistere se l'avvia non è fallito.

vmlinux
  Questo file contiene il kernel, e potrebbe essere utile da esaminare con
  programmi come pbjdump e gdb

Ci sono altri file, ma vengono usati meno. Molti sono utili all'analisi di
rcutorture stesso o dei suoi programmi.

Nel kernel v5.4, su un sistema a 12 processori, un'esecuzione senza errori
usando gli scenari di base produce il seguente risultato::

    SRCU-N ------- 804233 GPs (148.932/s) [srcu: g10008272 f0x0 ]
    SRCU-P ------- 202320 GPs (37.4667/s) [srcud: g1809476 f0x0 ]
    SRCU-t ------- 1122086 GPs (207.794/s) [srcu: g0 f0x0 ]
    SRCU-u ------- 1111285 GPs (205.794/s) [srcud: g1 f0x0 ]
    TASKS01 ------- 19666 GPs (3.64185/s) [tasks: g0 f0x0 ]
    TASKS02 ------- 20541 GPs (3.80389/s) [tasks: g0 f0x0 ]
    TASKS03 ------- 19416 GPs (3.59556/s) [tasks: g0 f0x0 ]
    TINY01 ------- 836134 GPs (154.84/s) [rcu: g0 f0x0 ] n_max_cbs: 34198
    TINY02 ------- 850371 GPs (157.476/s) [rcu: g0 f0x0 ] n_max_cbs: 2631
    TREE01 ------- 162625 GPs (30.1157/s) [rcu: g1124169 f0x0 ]
    TREE02 ------- 333003 GPs (61.6672/s) [rcu: g2647753 f0x0 ] n_max_cbs: 35844
    TREE03 ------- 306623 GPs (56.782/s) [rcu: g2975325 f0x0 ] n_max_cbs: 1496497
    CPU count limited from 16 to 12
    TREE04 ------- 246149 GPs (45.5831/s) [rcu: g1695737 f0x0 ] n_max_cbs: 434961
    TREE05 ------- 314603 GPs (58.2598/s) [rcu: g2257741 f0x2 ] n_max_cbs: 193997
    TREE07 ------- 167347 GPs (30.9902/s) [rcu: g1079021 f0x0 ] n_max_cbs: 478732
    CPU count limited from 16 to 12
    TREE09 ------- 752238 GPs (139.303/s) [rcu: g13075057 f0x0 ] n_max_cbs: 99011

Ripetizioni
===========

Immaginate di essere alla caccia di un raro problema che si verifica all'avvio.
Potreste usare kvm.sh, tuttavia questo ricompilerebbe il kernel ad ogni
esecuzione. Se avete bisogno di (diciamo) 1000 esecuzioni per essere sicuri di
aver risolto il problema, allora queste inutili ricompilazioni possono diventare
estremamente fastidiose.

Per questo motivo esiste kvm-again.sh.

Immaginate che un'esecuzione precedente di kvm.sh abbia lasciato i suoi
artefatti nella cartella::

	tools/testing/selftests/rcutorture/res/2022.11.03-11.26.28

Questa esecuzione può essere rieseguita senza ricompilazioni::

	kvm-again.sh tools/testing/selftests/rcutorture/res/2022.11.03-11.26.28

Alcuni dei parametri originali di kvm.sh possono essere sovrascritti, in
particolare --duration e --bootargs. Per esempio::

	kvm-again.sh tools/testing/selftests/rcutorture/res/2022.11.03-11.26.28 \
		--duration 45s

rieseguirebbe il test precedente, ma solo per 45 secondi, e quindi aiutando a
trovare quel raro problema all'avvio sopracitato.

Esecuzioni distribuite
======================

Sebbene kvm.sh sia utile, le sue verifiche sono limitate ad un singolo sistema.
Non è poi così difficile usare un qualsiasi ambiente di sviluppo per eseguire
(diciamo) 5 istanze di kvm.sh su altrettanti sistemi, ma questo avvierebbe
inutili ricompilazioni del kernel. In aggiunta, il processo di distribuzione
degli scenari di verifica per rcutorture sui sistemi disponibili richiede
scrupolo perché soggetto ad errori.

Per questo esiste kvm-remote.sh.

Se il seguente comando funziona::

	ssh system0 date

e funziona anche per system1, system2, system3, system4, e system5, e tutti
questi sistemi hanno 64 CPU, allora potere eseguire::

	kvm-remote.sh "system0 system1 system2 system3 system4 system5" \
		--cpus 64 --duration 8h --configs "5*CFLIST"

Questo compilerà lo scenario di base sul sistema locale, poi lo distribuirà agli
altri cinque sistemi elencati fra i parametri, ed eseguirà ogni scenario per
otto ore. Alla fine delle esecuzioni, i risultati verranno raccolti, registrati,
e stampati. La maggior parte dei parametri di kvm.sh possono essere usati con
kvm-remote.sh, tuttavia la lista dei sistemi deve venire sempre per prima.

L'argomento di kvm.sh ``--dryrun scenarios`` può essere utile per scoprire
quanti scenari potrebbero essere eseguiti in gruppo di sistemi.

Potete rieseguire anche una precedente esecuzione remota come abbiamo già fatto
per kvm.sh::

	kvm-remote.sh "system0 system1 system2 system3 system4 system5" \
		tools/testing/selftests/rcutorture/res/2022.11.03-11.26.28-remote \
		--duration 24h

In questo caso, la maggior parte dei parametri di kvm-again.sh possono essere
usati dopo il percorso alla cartella contenente gli artefatti dell'esecuzione da
ripetere.
