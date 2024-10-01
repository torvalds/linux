
.. include:: ../disclaimer-sp.rst

:Original: Documentation/scheduler/sched-eevdf.rst
:Translator: Sergio González Collado <sergio.collado@gmail.com>

======================
Gestor de tareas EEVDF
======================

El gestor de tareas EEVDF, del inglés: "Earliest Eligible Virtual Deadline
First", fue presentado por primera vez en una publicación científica en
1995 [1]. El kernel de Linux comenzó a transicionar hacia EEVPF en la
versión 6.6 (y como una nueva opción en 2024), alejándose del gestor
de tareas CFS, en favor de una versión de EEVDF propuesta por Peter
Zijlstra en 2023 [2-4]. Más información relativa a CFS puede encontrarse
en Documentation/scheduler/sched-design-CFS.rst.

De forma parecida a CFS, EEVDF intenta distribuir el tiempo de ejecución
de la CPU de forma equitativa entre todas las tareas que tengan la misma
prioridad y puedan ser ejecutables. Para eso, asigna un tiempo de
ejecución virtual a cada tarea, creando un "retraso" que puede ser usado
para determinar si una tarea ha recibido su cantidad justa de tiempo
de ejecución en la CPU. De esta manera, una tarea con un "retraso"
positivo, es porque se le debe tiempo de ejecución, mientras que una
con "retraso" negativo implica que la tarea ha excedido su cuota de
tiempo. EEVDF elige las tareas con un "retraso" mayor igual a cero y
calcula un tiempo límite de ejecución virtual (VD, del inglés: virtual
deadline) para cada una, eligiendo la tarea con la VD más próxima para
ser ejecutada a continuación. Es importante darse cuenta que esto permite
que la tareas que sean sensibles a la latencia que tengan porciones de
tiempos de ejecución de CPU más cortos ser priorizadas, lo cual ayuda con
su menor tiempo de respuesta.

Ahora mismo se está discutiendo cómo gestionar esos "retrasos", especialmente
en tareas que estén en un estado durmiente; pero en el momento en el que
se escribe este texto EEVDF usa un mecanismo de "decaimiento" basado en el
tiempo virtual de ejecución (VRT, del inglés: virtual run time). Esto previene
a las tareas de abusar del sistema simplemente durmiendo brevemente para
reajustar su retraso negativo: cuando una tarea duerme, esta permanece en
la cola de ejecución pero marcada para "desencolado diferido", permitiendo
a su retraso decaer a lo largo de VRT. Por tanto, las tareas que duerman
por más tiempo eventualmente eliminarán su retraso. Finalmente, las tareas
pueden adelantarse a otras si su VD es más próximo en el tiempo, y las
tareas podrán pedir porciones de tiempo específicas con la nueva llamada
del sistema sched_setattr(), todo esto facilitara el trabajo de las aplicaciones
que sean sensibles a las latencias.

REFERENCIAS
===========

[1] https://citeseerx.ist.psu.edu/document?repid=rep1&type=pdf&doi=805acf7726282721504c8f00575d91ebfd750564

[2] https://lore.kernel.org/lkml/a79014e6-ea83-b316-1e12-2ae056bda6fa@linux.vnet.ibm.com/

[3] https://lwn.net/Articles/969062/

[4] https://lwn.net/Articles/925371/
