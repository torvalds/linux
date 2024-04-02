.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-ita.rst

=======================
Statistiche sui blocchi
=======================

Cosa
====

Come suggerisce il nome, fornisce statistiche sui blocchi.


Perché
======

Perché, tanto per fare un esempio, le contese sui blocchi possono influenzare
significativamente le prestazioni.

Come
====

*Lockdep* ha punti di collegamento nelle funzioni di blocco e inoltre
mappa le istanze di blocco con le relative classi. Partiamo da questo punto
(vedere Documentation/translations/it_IT/locking/lockdep-design.rst).
Il grafico sottostante mostra la relazione che intercorre fra le
funzioni di blocco e i vari punti di collegamenti che ci sono al loro
interno::

        __acquire
            |
           lock _____
            |        \
            |    __contended
            |         |
            |       <wait>
            | _______/
            |/
            |
       __acquired
            |
            .
          <hold>
            .
            |
       __release
            |
         unlock

  lock, unlock	- le classiche funzioni di blocco
  __*		- i punti di collegamento
  <> 		- stati

Grazie a questi punti di collegamento possiamo fornire le seguenti statistiche:

con-bounces
  - numero di contese su un blocco che riguarda dati di un processore

contentions
  - numero di acquisizioni di blocchi che hanno dovuto attendere

wait time
  min
    - tempo minimo (diverso da zero) che sia mai stato speso in attesa di
      un blocco

  max
    - tempo massimo che sia mai stato speso in attesa di un blocco

  total
    - tempo totale speso in attesa di un blocco

  avg
	- tempo medio speso in attesa di un blocco

acq-bounces
  - numero di acquisizioni di blocco che riguardavano i dati su un processore

acquisitions
  - numero di volte che un blocco è stato ottenuto

hold time
  min
    - tempo minimo (diverso da zero) che sia mai stato speso trattenendo un blocco

  max
    - tempo massimo che sia mai stato speso trattenendo un blocco

  total
    - tempo totale di trattenimento di un blocco

  avg
    - tempo medio di trattenimento di un blocco

Questi numeri vengono raccolti per classe di blocco, e per ogni stato di
lettura/scrittura (quando applicabile).

Inoltre, questa raccolta di statistiche tiene traccia di 4 punti di contesa
per classe di blocco. Un punto di contesa è una chiamata che ha dovuto
aspettare l'acquisizione di un blocco.

Configurazione
--------------

Le statistiche sui blocchi si abilitano usando l'opzione di configurazione
CONFIG_LOCK_STAT.

Uso
---

Abilitare la raccolta di statistiche::

	# echo 1 >/proc/sys/kernel/lock_stat

Disabilitare la raccolta di statistiche::

	# echo 0 >/proc/sys/kernel/lock_stat

Per vedere le statistiche correnti sui blocchi::

  ( i numeri di riga non fanno parte dell'output del comando, ma sono stati
  aggiunti ai fini di questa spiegazione )

  # less /proc/lock_stat

  01 lock_stat version 0.4
  02-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  03                              class name    con-bounces    contentions   waittime-min   waittime-max waittime-total   waittime-avg    acq-bounces   acquisitions   holdtime-min   holdtime-max holdtime-total   holdtime-avg
  04-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  05
  06                         &mm->mmap_sem-W:            46             84           0.26         939.10       16371.53         194.90          47291        2922365           0.16     2220301.69 17464026916.32        5975.99
  07                         &mm->mmap_sem-R:            37            100           1.31      299502.61      325629.52        3256.30         212344       34316685           0.10        7744.91    95016910.20           2.77
  08                         ---------------
  09                           &mm->mmap_sem              1          [<ffffffff811502a7>] khugepaged_scan_mm_slot+0x57/0x280
  10                           &mm->mmap_sem             96          [<ffffffff815351c4>] __do_page_fault+0x1d4/0x510
  11                           &mm->mmap_sem             34          [<ffffffff81113d77>] vm_mmap_pgoff+0x87/0xd0
  12                           &mm->mmap_sem             17          [<ffffffff81127e71>] vm_munmap+0x41/0x80
  13                         ---------------
  14                           &mm->mmap_sem              1          [<ffffffff81046fda>] dup_mmap+0x2a/0x3f0
  15                           &mm->mmap_sem             60          [<ffffffff81129e29>] SyS_mprotect+0xe9/0x250
  16                           &mm->mmap_sem             41          [<ffffffff815351c4>] __do_page_fault+0x1d4/0x510
  17                           &mm->mmap_sem             68          [<ffffffff81113d77>] vm_mmap_pgoff+0x87/0xd0
  18
  19.............................................................................................................................................................................................................................
  20
  21                         unix_table_lock:           110            112           0.21          49.24         163.91           1.46          21094          66312           0.12         624.42       31589.81           0.48
  22                         ---------------
  23                         unix_table_lock             45          [<ffffffff8150ad8e>] unix_create1+0x16e/0x1b0
  24                         unix_table_lock             47          [<ffffffff8150b111>] unix_release_sock+0x31/0x250
  25                         unix_table_lock             15          [<ffffffff8150ca37>] unix_find_other+0x117/0x230
  26                         unix_table_lock              5          [<ffffffff8150a09f>] unix_autobind+0x11f/0x1b0
  27                         ---------------
  28                         unix_table_lock             39          [<ffffffff8150b111>] unix_release_sock+0x31/0x250
  29                         unix_table_lock             49          [<ffffffff8150ad8e>] unix_create1+0x16e/0x1b0
  30                         unix_table_lock             20          [<ffffffff8150ca37>] unix_find_other+0x117/0x230
  31                         unix_table_lock              4          [<ffffffff8150a09f>] unix_autobind+0x11f/0x1b0

Questo estratto mostra le statistiche delle prime due classi di
blocco. La riga 01 mostra la versione dell'output - la versione
cambierà ogni volta che cambia il formato. Le righe dalla 02 alla 04
rappresentano l'intestazione con la descrizione delle colonne. Le
statistiche sono mostrate nelle righe dalla 05 alla 18 e dalla 20
alla 31. Queste statistiche sono divise in due parti: le statistiche,
seguite dai punti di contesa (righe 08 e 13) separati da un divisore.

Le righe dalla 09 alla 12 mostrano i primi quattro punti di contesa
registrati (il codice che tenta di acquisire un blocco) e le righe
dalla 14 alla 17 mostrano i primi quattro punti contesi registrati
(ovvero codice che ha acquisito un blocco). È possibile che nelle
statistiche manchi il valore *max con-bounces*.

Il primo blocco (righe dalla 05 alla 18) è di tipo lettura/scrittura e quindi
mostra due righe prima del divisore. I punti di contesa non corrispondono alla
descrizione delle colonne nell'intestazione; essi hanno due colonne: *punti di
contesa* e *[<IP>] simboli*. Il secondo gruppo di punti di contesa sono i punti
con cui si contende il blocco.

La parte interna del tempo è espressa in us (microsecondi).

Quando si ha a che fare con blocchi annidati si potrebbero vedere le
sottoclassi di blocco::

  32...........................................................................................................................................................................................................................
  33
  34                               &rq->lock:       13128          13128           0.43         190.53      103881.26           7.91          97454        3453404           0.00         401.11    13224683.11           3.82
  35                               ---------
  36                               &rq->lock          645          [<ffffffff8103bfc4>] task_rq_lock+0x43/0x75
  37                               &rq->lock          297          [<ffffffff8104ba65>] try_to_wake_up+0x127/0x25a
  38                               &rq->lock          360          [<ffffffff8103c4c5>] select_task_rq_fair+0x1f0/0x74a
  39                               &rq->lock          428          [<ffffffff81045f98>] scheduler_tick+0x46/0x1fb
  40                               ---------
  41                               &rq->lock           77          [<ffffffff8103bfc4>] task_rq_lock+0x43/0x75
  42                               &rq->lock          174          [<ffffffff8104ba65>] try_to_wake_up+0x127/0x25a
  43                               &rq->lock         4715          [<ffffffff8103ed4b>] double_rq_lock+0x42/0x54
  44                               &rq->lock          893          [<ffffffff81340524>] schedule+0x157/0x7b8
  45
  46...........................................................................................................................................................................................................................
  47
  48                             &rq->lock/1:        1526          11488           0.33         388.73      136294.31          11.86          21461          38404           0.00          37.93      109388.53           2.84
  49                             -----------
  50                             &rq->lock/1        11526          [<ffffffff8103ed58>] double_rq_lock+0x4f/0x54
  51                             -----------
  52                             &rq->lock/1         5645          [<ffffffff8103ed4b>] double_rq_lock+0x42/0x54
  53                             &rq->lock/1         1224          [<ffffffff81340524>] schedule+0x157/0x7b8
  54                             &rq->lock/1         4336          [<ffffffff8103ed58>] double_rq_lock+0x4f/0x54
  55                             &rq->lock/1          181          [<ffffffff8104ba65>] try_to_wake_up+0x127/0x25a

La riga 48 mostra le statistiche per la seconda sottoclasse (/1) della
classe *&irq->lock* (le sottoclassi partono da 0); in questo caso,
come suggerito dalla riga 50, ``double_rq_lock`` tenta di acquisire un blocco
annidato di due spinlock.

Per vedere i blocco più contesi::

  # grep : /proc/lock_stat | head
			clockevents_lock:       2926159        2947636           0.15       46882.81  1784540466.34         605.41        3381345        3879161           0.00        2260.97    53178395.68          13.71
		     tick_broadcast_lock:        346460         346717           0.18        2257.43    39364622.71         113.54        3642919        4242696           0.00        2263.79    49173646.60          11.59
		  &mapping->i_mmap_mutex:        203896         203899           3.36      645530.05 31767507988.39      155800.21        3361776        8893984           0.17        2254.15    14110121.02           1.59
			       &rq->lock:        135014         136909           0.18         606.09      842160.68           6.15        1540728       10436146           0.00         728.72    17606683.41           1.69
	       &(&zone->lru_lock)->rlock:         93000          94934           0.16          59.18      188253.78           1.98        1199912        3809894           0.15         391.40     3559518.81           0.93
			 tasklist_lock-W:         40667          41130           0.23        1189.42      428980.51          10.43         270278         510106           0.16         653.51     3939674.91           7.72
			 tasklist_lock-R:         21298          21305           0.20        1310.05      215511.12          10.12         186204         241258           0.14        1162.33     1179779.23           4.89
			      rcu_node_1:         47656          49022           0.16         635.41      193616.41           3.95         844888        1865423           0.00         764.26     1656226.96           0.89
       &(&dentry->d_lockref.lock)->rlock:         39791          40179           0.15        1302.08       88851.96           2.21        2790851       12527025           0.10        1910.75     3379714.27           0.27
			      rcu_node_0:         29203          30064           0.16         786.55     1555573.00          51.74          88963         244254           0.00         398.87      428872.51           1.76

Per cancellare le statistiche::

  # echo 0 > /proc/lock_stat
