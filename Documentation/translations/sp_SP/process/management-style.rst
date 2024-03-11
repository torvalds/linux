.. include:: ../disclaimer-sp.rst

:Original: Documentation/process/management-style.rst
:Translator: Avadhut Naik <avadhut.naik@amd.com>

.. _sp_managementstyle:


Estilo de gestión del kernel de Linux
=====================================

Este es un documento breve que describe el estilo de gestión preferido (o
inventado, dependiendo de a quién le preguntes) para el kernel de Linux.
Está destinado a reflejar el documento
:ref:`translations/sp_SP/process/coding-style.rst <sp_codingstyle>` hasta
cierto punto y está escrito principalmente para evitar responder a [#f1]_
las mismas preguntas (o similares) una y otra vez.

El estilo de gestión es muy personal y mucho más difícil de cuantificar
que reglas simples de estilo de codificación, por lo que este documento
puede o no tener relación con la realidad. Comenzó como una broma, pero
eso no significa que no pueda ser realmente cierto. Tendrás que decidir
por ti mismo.

Por cierto, cuando se hable de “gerente de kernel”, se refiere a las
personas lideres técnicas, no de las personas que hacen la gestión
tradicional dentro de las empresas. Si firmas pedidos de compra o tienes
alguna idea sobre el presupuesto de tu grupo, es casi seguro que no eres
un gerente de kernel. Estas sugerencias pueden o no aplicarse a usted.

En primer lugar, sugeriría comprar “Seven Habits of Highly Effective
People” y NO leerlo. Quemarlo, es un gran gesto simbólico.

.. [#f1] Este documento lo hace no tanto respondiendo a la pregunta, sino
  haciendo dolorosamente obvio para el interrogador que no tenemos ni idea
  de cuál es la respuesta.

De todos modos, aquí va:

.. _decisiones:

1) Decisiones
-------------

Todos piensan que los gerentes toman decisiones, y que la toma de
decisiones en importante. Cuanto más grande y dolorosa sea la decisión,
más grande debe ser el gerente para tomarla. Eso es muy profundo y obvio,
pero en realidad no es cierto.

El nombre del partido es **evitar** tener que tomar una decisión. En
particular, si alguien te dice “elige (a) o (b), realmente necesitamos
que decidas sobre esto”, estas en problemas como gerente. Es mejor que
las personas a las que diriges conozcan los detalles mejor que tú, así
que, si acuden a ti para tomar una decisión técnica, estas jodido.
Claramente no eres competente para tomar una decisión por ellos.

(Corolario: Si las personas a las que diriges no conocen los detalles
mejor que tú, también estas jodido, aunque por una razón totalmente
diferente. Es decir, que estas en el trabajo equivocado y que **ellos**
deberían gestionando tu brillantez en su lugar).

Así que el nombre del partido es **evitar** las decisiones, al menos las
grandes y dolorosas. Tomar decisiones pequeñas y sin consecuencias está
bien, y te hace parecer que sabes lo que estás haciendo, así que lo que
un gerente de kernel necesita hacer es convertir las decisiones grandes
y dolorosas en cosas pequeñas a los que a nadie realmente le importa.

Ayuda darse cuenta de que la diferencia clave entre una decisión grande
y una pequeña es si puede arreglar su decisión después. Cualquier
decisión se puede hacer pequeña simplemente asegurándose siempre de que
si te equivocaste (u **estarás** equivocado), siempre puede deshacer el
daño más tarde retrocediendo. De repente, llegas a ser doblemente
gerencial por tomar **dos** decisiones intrascendentes - la equivocada
**y** la correcta.

Y las personas incluso verán eso como un verdadero liderazgo (*tos*
mierda *tos*).

Por lo tanto, la llave para evitar las grandes decisiones se convierte en
simplemente evitar hacer cosas que no se pueden deshacer. No te dejes
llevar a una esquina del que no puedas escapar. Una rata acorralada puede
ser peligrosa – un gerente acorralado es directamente lamentable.

Resulta que, dado que nadie sería tan estúpido como para dejar que un
gerente de kernel tenga una gran responsabilidad **de todos modos**,
generalmente es bastante fácil retroceder. Dado que no vas a poder
malgastar grandes cantidades de dinero que tal vez no puedas pagar, lo
único que puedes revertir es una decisión técnica, y ahí retroceder es
muy fácil: simplemente diles a todos que fuiste un bobo incompetente,
pide disculpas y deshaz todo el trabajo inútil que hiciste trabajar a la
gente durante el año pasado. De repente, la decisión que tomaste hace un
año no era una gran decisión después de todo, ya que se podía deshacer
fácilmente.

Resulta que algunas personas tienen problemas con este enfoque, por dos
razones:

 - admitir que eras un idiota es más difícil de lo que parece. A todos
   nos gusta mantener las apariencias, y salir en público a decir que te
   equivocaste a veces es muy duro.
 - que alguien te diga que lo que trabajaste durante el último año no
   valió la pena después de todo también puede ser duro para los pobres
   ingenieros humildes, y aunque el **trabajo** real fue bastante fácil
   de deshacer simplemente eliminándolo, es posible que hayas perdido
   irrevocablemente la confianza de ese ingeniero. Y recuerda:
   “irrevocablemente” fue lo que tratamos de evitar en primer lugar, y
   tu decisión terminó siendo muy grande después de todo.

Afortunadamente, estas dos razones pueden mitigarse eficazmente
simplemente admitiendo inicialmente que no tienes ni idea, y diciéndole
a la gente que tu decisión es puramente preliminar, y podría ser la cosa
equivocada. Siempre te debes reservar el derecho de cambiar de opinión, y
hacer que la gente sea muy **consciente** de eso. Y es mucho más fácil
admitir que eres estúpido cuando **aun** no has hecho la cosa realmente
estúpida.

Entonces, cuando realmente resulta ser estúpido, la gente simplemente
pone los ojos y dice “Ups, otra vez no”.

Esta admisión preventiva de incompetencia también podría hacer que las
personas que realmente hacen el trabajo piensen dos veces sobre si vale la
pena hacerlo o no. Después de todo, si **ellos** no están seguros de si es
una buena idea, seguro que no deberías alentarlos prometiéndoles que lo
que trabajan será incluido. Haz que al menos lo piensen dos veces antes de
embarcarse en un gran esfuerzo.

Recuerda: Es mejor que sepan más sobre los detalles que tú, y
generalmente ya piensan que tienen la respuesta a todo. Lo mejor que puede
hacer como gerente no es inculcar confianza, sino más bien una dosis
saludable de pensamiento crítico sobre lo que hacen.

Por cierto, otra forma de evitar una decisión es quejarse lastimeramente
de “no podemos hacer ambas cosas?” y parecer lamentable. Créeme, funciona.
Si no está claro cuál enfoque es mejor, lo descubrirán. La respuesta puede
terminar siendo que ambos equipos se sientan tan frustrados por la
situación que simplemente se den por vencidos.

Eso puede sonar como un fracaso, pero generalmente es una señal de que
había algo mal con ambos proyectos, y la razón por la que las personas
involucradas no pudieron decidir fue que ambos estaban equivocados.
Terminas oliendo a rosas y evitaste otra decisión que podrías haber
metido la pata.

2) Gente
--------

La mayoría de las personas son idiotas, y ser gerente significa que
tendrás que lidiar con eso, y quizás lo más importante, que **ellos**
tienen que lidiar **contigo**.

Resulta que, si bien es fácil deshacer los errores técnicos, no es tan
fácil deshacer los trastornos de personalidad. Solo tienes que vivir
con los suyos - y el tuyo.

Sin embargo, para prepararse como gerente del kernel, es mejor recordar
no quemar ningún puente, bombardear a ningún aldeano inocente o alienar
a demasiados desarrolladores del kernel. Resulta que alienar a las
personas es bastante fácil, y desalienarlas es difícil. Por lo tanto,
“alienar” cae inmediatamente debajo del título “no reversible”, y se
convierte en un no-no según :ref:`decisiones`.

Aquí solo hay algunas reglas simples:

 (1) No llames a la gente pen*ejos (al menos no en público)
 (2) Aprende a disculparte cuando olvidaste la regla (1)

El problema con #1 es que es muy fácil de hacer, ya que puedes decir
“eres un pen*ejo” de millones de manera diferentes [#f2]_, a veces sin
siquiera darte cuenta, y casi siempre con una convicción ardiente de que
tienes razón.

Y cuanto más convencido estés de que tienes razón (y seamos sinceros,
puedes llamar a casi **cualquiera** un pen*ejo, y a menudo **tendrás**
razón), más difícil termina siendo disculparse después.

Para resolver este problema, realmente solo tienes dos opciones:

 - Se muy buenos en las disculpas.
 - Difunde el “amor” de manera tan uniforme que nadie termina sintiendo
   que es atacado injustamente. Hazlo lo suficientemente ingenioso, e
   incluso podría divertirse.

La opción de ser infaliblemente educado realmente no existe. Nadie
confiará en alguien que está ocultando tan claramente su verdadero
carácter.

.. [#f2] Paul Simon cantó “Cincuenta maneras de dejar a tu amante” porque,
  francamente, “Un millón de maneras de decirle a un desarrollador que es
  un pen*ejo” no escanea tan bien. Pero estoy seguro de que lo pensó.

3) Gente II – el Buen Tipo
--------------------------

Aunque resulta que la mayoría de las personas son idiotas, el corolario
de eso es, tristemente, que tú también seas uno, y aunque todos podemos
disfrutar del conocimiento seguro de que somos mejores que la persona
promedio (somos realistas, nadie cree que nunca que son promedio o debajo
del promedio), también debemos admitir que no somos el cuchillo más
afilado alrededor, y habrá otras personas que son menos idiotas que tú.

Algunas personas reaccionan mal a las personas inteligentes. Otras se
aprovechan de ellos.

Asegúrate de que tú, como mantenedor del kernel, estás en el segundo
grupo. Aguanta con ellos, porque son las personas que te facilitarán el
trabajo. En particular, podrán tomar tus decisiones por ti, que es de lo
que se trata el juego.

Así que cuando encuentras a alguien más inteligente que tú, simplemente
sigue adelante. Sus responsabilidades de gestión se convierten en gran
medida en las de decir “Suena como una buena idea, - hazlo sin
restricciones”, o “Eso suena bien, pero ¿qué pasa con xxx?". La segunda
versión en particular es una excelente manera de aprender algo nuevo
sobre “xxx” o parecer **extra** gerencial al señalar algo que la persona
más inteligente no había pensado. En cualquier caso, sales ganando.

Una cosa para tener en cuenta es darse cuenta de que la grandeza en un
área no necesariamente se traduce en otras áreas. Así que puedes impulsar
a la gente en direcciones específicas, pero seamos realistas, pueden ser
buenos en lo que hacen, y ser malos en todo lo demás. La buena noticia es
que las personas tienden a gravitar naturalmente hacia lo que son buenos,
por lo que no es como si estuvieras haciendo algo irreversible cuando los
impulsas en alguna dirección, simplemente no presiones demasiado.

4) Colocar la culpa
-------------------

Las cosas saldrán mal, y la gente quiere culpar a alguien. Etiqueta, tú
lo eres.

En realidad, no es tan difícil aceptar la culpa, especialmente si la gente
se da cuenta de que no fue **toda** tu culpa. Lo que nos lleva a la mejor
manera de asumir la culpa: hacerlo por otra persona. Te sentirás bien por
asumir la caída, ellos se sentirán bien por no ser culpados, y la persona
que perdió toda su colección de pornografía de 36 GB debido a tu
incompetencia admitirá a regañadientes que al menos intentaste escapar
de ella.

Luego haz que el desarrollador que realmente metió la pata (si puedes
encontrarlo) sepa **en privado** que metió la pata. No solo para que
pueda evitarlo en futuro, sino para que sepan que te deben uno. Y, quizás
aún más importante, también es probable que sea la persona que puede
solucionarlo. Porque, seamos sinceros, seguro que no eres tú.

Asumir la culpa también es la razón por la que llegas a ser un gerente
en primer lugar. Es parte de lo que hace que la gente confíe en ti y te
permita la gloria potencial porque eres tú quien puede decir “metí la
pata”. Y si has seguido las reglas anteriores, ya serás bastante bueno
para decir eso.

5) Cosas que evitar
-------------------

Hay una cosa que la gente odia incluso más que ser llamado “pen*ejo”,
y que es ser llamado “pen*ejo” en una voz mojigata. Por lo primero,
puedes disculparte, por lo segundo, realmente, no tendrás la oportunidad.
Es probable que ya no estén escuchando, incluso si de lo contrario haces
un buen trabajo.

Todos pensamos que somos mejores que los demás, lo que significa que
cuando alguien más se da aires, **realmente** nos molesta. Puedes ser
moral e intelectualmente superior a todos los que te rodean, pero no
trates de hacerlo demasiado obvio a menos que tengas **la intención**
real de irritar a alguien [#f3]_.

Del mismo modo, no seas demasiado educado o sutil acerca de las cosas. La
cortesía fácilmente termina yendo demasiado lejos y ocultado el problema,
y como dicen “En internet, nadie puede oírte ser sutil”. Usa un gran
objeto contundente para enfatizar el punto, porque realmente no puedes
depender de que las personas entiendan tu punto de otra manera.

Un poco de humor puede ayudar a suavizar tanto la franqueza como la
moralización. Exagerar hasta el punto de ser ridículo puede reforzar un
punto sin hacer que sea doloroso para el destinatario, quien simplemente
piensa que estas siendo tonto. Por lo tanto, puede ayudarnos a superar el
bloqueo mental personal que todos tenemos sobre la crítica.

.. [#f3] La pista: Los grupos de noticias de Internet que no están
  directamente relacionados con tu trabajo son excelentes maneras de
  desahogar tus frustraciones con otras personas. Escribe mensajes
  insultantes con una mueca de desprecio solo para entrar en un humor de
  vez en cuando, y te sentirás limpio. Eso sí, no te cagues demasiado
  cerca de casa.

6) ¿Por qué a mí?
-----------------

Dado que tu principal responsabilidad parece ser asumir la culpa de los
errores de otras personas y hacer dolorosamente obvio para todos los
demás que eres incompetente, la pregunta obvia es: ¿por qué hacerlo en
primer lugar?

Pase lo que pase, **tendrás** una sensación inmensa de logro personal por
estar “a cargo”. No importa el hecho de que realmente estés liderando al
tratar de mantenerte al día con todos los demás y correr detrás de ellos
lo más rápido que puedes. Todo el mundo seguirá pensando que eres la
persona a cargo.

Es un gran trabajo si puedes descifrarlo.
