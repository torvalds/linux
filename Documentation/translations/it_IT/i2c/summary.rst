==========================
Introduzione a I2C e SMBus
==========================

I²C (letteralmente "I al quadrato C" e scritto I2C nella documentazione del
kernel) è un protocollo sviluppato da Philips. É un protocollo a 2 fili (a
velocità variabile, solitamente fino a 400KHz, e in modalità alta velocità fino
a 5 MHz). Questo protocollo offre un bus a basso costo per collegare dispositivi
di vario genere a cui si accede sporadicamente e utilizzando poca banda. I2C è
ampiamente usato nei sistemi integrati. Alcuni sistemi usano varianti che non
rispettano i requisiti originali, per cui non sono indicati come I2C, ma hanno
nomi diversi, per esempio TWI (Interfaccia a due fili), IIC.

L'ultima specifica ufficiale I2C è la `"Specifica I2C-bus e manuale utente"
(UM10204) <https://www.nxp.com/docs/en/user-guide/UM10204.pdf>`_ pubblicata da
NXP Semiconductors, al momento della scrittura si tratta della versione 7

SMBus (Bus per la gestione del sistema) si basa sul protocollo I2C ed è
principalmente un sottoinsieme di protocolli e segnali I2C. Molti dispositivi
I2C funzioneranno su SMBus, ma alcuni protocolli SMBus aggiungono semantica
oltre quanto richiesto da I2C. Le moderne schede madri dei PC si affidano a
SMBus. I più comuni dispositivi collegati tramite SMBus sono moduli RAM
configurati utilizzando EEPROM I2C, e circuiti integrati di monitoraggio
hardware.

Poiché SMBus è principalmente un sottoinsieme del bus I2C, possiamo farne uso su
molti sistemi I2C. Ci sono però sistemi che non soddisfano i vincoli elettrici
sia di SMBus che di I2C; e altri che non possono implementare tutta la semantica
o messaggi comuni del protocollo SMBus.


Terminologia
============

Il bus I2C connette uno o più circuiti integrati controllori a dei dispositivi.

.. kernel-figure::  ../../../i2c/i2c_bus.svg
   :alt:    Un semplice bus I2C con un controllore e 3 dispositivi

   Un semplice Bus I2C

Un circuito integrato **controllore** (*controller*) è un nodo che inizia le
comunicazioni con i dispositivi (*targets*). Nell'implementazione del kernel
Linux è chiamato **adattatore** o bus. I driver degli adattatori si trovano
nella sottocartella ``drivers/i2c/busses/``.

Un **algoritmo** contiene codice generico che può essere utilizzato per
implementare una intera classe di adattatori I2C. Ciascun driver dell'
adattatore specifico dipende da un driver dell'algoritmo nella sottocartella
``drivers/i2c/algos/`` o include la propria implementazione.

Un circuito integrato **dispositivo** è un nodo che risponde alle comunicazioni
quando indirizzato dal controllore. In Linux è chiamato **client**. Nonostante i
dispositivi siano circuiti integrati esterni al sistema, Linux può agire come
dispositivo (se l'hardware lo permette) e rispondere alla richieste di altri
controllori sul bus. Questo verrà chiamato **dispositivo locale** (*local
target*). Negli altri casi si parla di **dispositivo remoto** (*remote target*).

I driver dei dispositivi sono contenuti in una cartella specifica per la
funzionalità che forniscono, ad esempio ``drivers/media/gpio/`` per espansori
GPIO e ``drivers/media/i2c/`` per circuiti integrati relativi ai video.

Per la configurazione di esempio in figura, avrai bisogno di un driver per il
tuo adattatore I2C e driver per i tuoi dispositivi I2C (solitamente un driver
per ciascuno dispositivo).

Sinonimi
--------

Come menzionato precedentemente, per ragioni storiche l'implementazione I2C del
kernel Linux usa "adatattore" (*adapter*) per i controllori e "client" per i
dispositivi. Un certo numero di strutture dati usano questi sinonimi nei loro
nomi. Dunque, durante le discussioni riguardanti l'implementazione dovrete
essere a coscienza anche di questi termini. Tuttavia si preferiscono i termini
ufficiali.

Terminologia obsoleta
---------------------

Nelle prime specifiche di I2C, il controllore veniva chiamato "master" ed i
dispositivi "slave". Questi termini sono stati resi obsoleti con la versione 7
della specifica. Inoltre, il loro uso viene scoraggiato dal codice di condotta
del kernel Linux. Tuttavia, potreste ancora trovare questi termini in pagine non
aggiornate. In generale si cerca di usare i termini controllore e dispositivo.
