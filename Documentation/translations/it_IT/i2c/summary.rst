==========================
Introduzione a I2C e SMBus
==========================

I²C (letteralmente "I al quadrato C" e scritto I2C nella documentazione del
kernel) è un protocollo sviluppato da Philips. É un protocollo lento a 2 fili
(a velocità variabile, al massimo 400KHz), con un'estensione per le velocità
elevate (3.4 MHz). Questo protocollo offre un bus a basso costo per collegare
dispositivi di vario genere a cui si accede sporadicamente e utilizzando
poca banda. Alcuni sistemi usano varianti che non rispettano i requisiti
originali, per cui non sono indicati come I2C, ma hanno nomi diversi, per
esempio TWI (Interfaccia a due fili), IIC.

L'ultima specifica ufficiale I2C è la `"Specifica I2C-bus e manuale utente"
(UM10204) <https://www.nxp.com/webapp/Download?colCode=UM10204>`_
pubblicata da NXP Semiconductors. Tuttavia, è necessario effettuare il login
al sito per accedere al PDF. Una versione precedente della specifica
(revisione 6) è archiviata
`qui <https://web.archive.org/web/20210813122132/
https://www.nxp.com/docs/en/user-guide/UM10204.pdf>`_.

SMBus (Bus per la gestione del sistema) si basa sul protocollo I2C ed è
principalmente un sottoinsieme di protocolli e segnali I2C. Molti dispositivi
I2C funzioneranno su SMBus, ma alcuni protocolli SMBus aggiungono semantica
oltre quanto richiesto da I2C. Le moderne schede madri dei PC si affidano a
SMBus. I più comuni dispositivi collegati tramite SMBus sono moduli RAM
configurati utilizzando EEPROM I2C, e circuiti integrati di monitoraggio
hardware.

Poiché SMBus è principalmente un sottoinsieme del bus I2C,
possiamo farne uso su molti sistemi I2C. Ci sono però sistemi che non
soddisfano i vincoli elettrici sia di SMBus che di I2C; e altri che non possono
implementare tutta la semantica o messaggi comuni del protocollo SMBus.


Terminologia
============

Utilizzando la terminologia della documentazione ufficiale, il bus I2C connette
uno o più circuiti integrati *master* e uno o più circuiti integrati *slave*.

.. kernel-figure::  ../../../i2c/i2c_bus.svg
   :alt:    Un semplice bus I2C con un master e 3 slave

   Un semplice Bus I2C

Un circuito integrato  **master** è un nodo che inizia le comunicazioni con gli
slave. Nell'implementazione del kernel Linux è chiamato **adattatore** o bus. I
driver degli adattatori si trovano nella sottocartella ``drivers/i2c/busses/``.

Un **algoritmo** contiene codice generico che può essere utilizzato per
implementare una intera classe di adattatori I2C. Ciascun driver dell'
adattatore specifico dipende da un driver dell'algoritmo nella sottocartella
``drivers/i2c/algos/`` o include la propria implementazione.

Un circuito integrato **slave** è un nodo che risponde alle comunicazioni
quando indirizzato dal master. In Linux è chiamato **client** (dispositivo). I
driver dei dispositivi sono contenuti in una cartella specifica per la
funzionalità che forniscono, ad esempio ``drivers/media/gpio/`` per espansori
GPIO e ``drivers/media/i2c/`` per circuiti integrati relativi ai video.

Per la configurazione di esempio in figura, avrai bisogno di un driver per il
tuo adattatore I2C e driver per i tuoi dispositivi I2C (solitamente un driver
per ciascuno dispositivo).
